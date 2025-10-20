using System.Buffers.Binary;
using System.Globalization;
using System.Linq;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Threading;

namespace LiveQuotesSample;

internal static class Program
{
    private const decimal PriceScale = 1_000_000_000m;
    private const string DefaultClientTag = "Databento C# Live Sample";

    public static void Main(string[] args)
    {
        try
        {
            var options = Options.Parse(args);
            if (string.IsNullOrEmpty(options.ApiKey))
            {
                options.ApiKey = Environment.GetEnvironmentVariable("DATABENTO_API_KEY");
            }

            if (string.IsNullOrEmpty(options.ApiKey))
            {
                Console.Error.WriteLine("An API key must be supplied via --api-key or DATABENTO_API_KEY.");
                Environment.ExitCode = 1;
                return;
            }

            if (options.Symbols.Count == 0)
            {
                Console.Error.WriteLine("At least one symbol is required (use --symbols or the default ES.FUT).");
                Environment.ExitCode = 1;
                return;
            }

            var gateway = string.IsNullOrEmpty(options.Gateway)
                ? BuildGateway(options.Dataset)
                : options.Gateway;

            Console.WriteLine($"Connecting to {gateway}:{options.Port} for dataset {options.Dataset}...");
            using var tcpClient = new TcpClient();
            tcpClient.Connect(gateway, options.Port);
            using var networkStream = tcpClient.GetStream();
            networkStream.ReadTimeout = Timeout.Infinite;
            networkStream.WriteTimeout = Timeout.Infinite;

            var sessionId = Authenticate(networkStream, options);
            Console.WriteLine($"Authenticated with session {sessionId}.");

            SendSubscription(networkStream, options);
            Console.WriteLine(
                $"Subscribed to {string.Join(',', options.Symbols)} as {options.Schema} ({options.StypeIn}).");

            WriteCommand(networkStream, "start_session\n");

            var metadata = DbnMetadataReader.Read(networkStream);
            Console.WriteLine(
                $"Gateway metadata: dataset={metadata.Dataset}, stype_out={metadata.StypeOut}, symbol_cstr_len={metadata.SymbolCStringLength}.");

            var instrumentSymbols = new Dictionary<uint, string>();

            var reader = new DbnRecordReader(networkStream);
            var printed = 0;
            Console.WriteLine("Waiting for live records... (press Ctrl+C to exit)");
            Console.CancelKeyPress += (_, eventArgs) =>
            {
                eventArgs.Cancel = true;
                reader.Cancel();
            };

            while (!reader.IsCanceled && (options.MaxRecords <= 0 || printed < options.MaxRecords))
            {
                var record = reader.Read();
                if (record is null)
                {
                    break;
                }

                switch (record.Value.Header.RType)
                {
                    case RType.InstrumentDef:
                    {
                        var symbol = RecordDecoders.ParseInstrumentDefinition(record.Value.PayloadSpan);
                        if (!string.IsNullOrEmpty(symbol))
                        {
                            instrumentSymbols[record.Value.Header.InstrumentId] = symbol;
                        }

                        break;
                    }
                    case RType.SymbolMapping:
                    {
                        // Symbol mappings are optional for this sample, but parsing them here
                        // can help when extending the example.
                        var mapping = RecordDecoders.ParseSymbolMapping(record.Value.PayloadSpan);
                        if (mapping is not null && options.Verbose)
                        {
                            Console.WriteLine(
                                $"[SYMBOL_MAPPING] {mapping.Value.InputType}:{mapping.Value.InputSymbol} -> {mapping.Value.OutputType}:{mapping.Value.OutputSymbol}");
                        }

                        break;
                    }
                    case RType.Error:
                    {
                        var err = RecordDecoders.ParseError(record.Value.PayloadSpan);
                        Console.Error.WriteLine($"Gateway error: {err}");
                        reader.Cancel();
                        break;
                    }
                    case RType.System:
                    {
                        var systemMessage = RecordDecoders.ParseSystem(record.Value.PayloadSpan);
                        if (options.Verbose || systemMessage.IsHeartbeat)
                        {
                            Console.WriteLine($"[SYSTEM] {systemMessage.Message}");
                        }

                        break;
                    }
                    case RType.Mbp1:
                    {
                        var quote = RecordDecoders.ParseMbp1(record.Value.PayloadSpan);
                        printed++;

                        var iid = record.Value.Header.InstrumentId;
                        var symbol = instrumentSymbols.TryGetValue(iid, out var resolved)
                            ? resolved
                            : iid.ToString(CultureInfo.InvariantCulture);

                        var ts = ToIsoString(record.Value.Header.TsEvent);
                        var bid = quote.BestBid / PriceScale;
                        var ask = quote.BestAsk / PriceScale;
                        Console.WriteLine(
                            $"[{ts}] {symbol} bid={bid:F4} x {quote.BestBidSize} ask={ask:F4} x {quote.BestAskSize} action={quote.Action} side={quote.Side} seq={quote.Sequence}");
                        break;
                    }
                    default:
                    {
                        // Skip other record types.
                        break;
                    }
                }
            }

            Console.WriteLine($"Processed {printed} quote updates.");
        }
        catch (OperationCanceledException)
        {
            Console.WriteLine("Cancelled by user.");
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Unhandled exception: {ex.Message}");
            Console.Error.WriteLine(ex);
            Environment.ExitCode = 1;
        }
    }

    private static string BuildGateway(string dataset)
    {
        var builder = new StringBuilder(dataset.Length + 20);
        foreach (var c in dataset)
        {
            builder.Append(c == '.' ? '-' : char.ToLowerInvariant(c));
        }

        builder.Append(".lsg.databento.com");
        return builder.ToString();
    }

    private static ulong Authenticate(NetworkStream networkStream, Options options)
    {
        var greeting = ReadAsciiLine(networkStream);
        if (!string.IsNullOrEmpty(greeting))
        {
            Console.WriteLine($"Gateway greeting: {greeting}");
        }

        string? challenge = null;
        while (challenge is null)
        {
            var line = ReadAsciiLine(networkStream);
            if (line.StartsWith("cram=", StringComparison.Ordinal))
            {
                challenge = line.Substring("cram=".Length);
            }
            else if (line.Length == 0)
            {
                continue;
            }
            else
            {
                Console.WriteLine($"Gateway notice: {line}");
            }
        }

        var cram = GenerateCramReply(challenge, options.ApiKey!);
        var authBuilder = new StringBuilder();
        authBuilder.Append("auth=").Append(cram)
            .Append("|dataset=").Append(options.Dataset)
            .Append("|encoding=dbn")
            .Append("|ts_out=").Append(options.SendTsOut ? '1' : '0')
            .Append("|client=").Append(DefaultClientTag);
        if (!string.IsNullOrEmpty(options.ClientExtension))
        {
            authBuilder.Append(' ').Append(options.ClientExtension);
        }

        if (options.HeartbeatIntervalSeconds is int heartbeatSeconds)
        {
            authBuilder.Append("|heartbeat_interval_s=").Append(heartbeatSeconds);
        }

        authBuilder.Append('\n');
        WriteCommand(networkStream, authBuilder.ToString());

        var response = ReadAsciiLine(networkStream);
        var fields = response.Split('|', StringSplitOptions.RemoveEmptyEntries);
        var success = false;
        ulong sessionId = 0;
        string? error = null;
        foreach (var field in fields)
        {
            var kv = field.Split('=', 2, StringSplitOptions.RemoveEmptyEntries);
            if (kv.Length != 2)
            {
                continue;
            }

            switch (kv[0])
            {
                case "success":
                    success = kv[1] == "1";
                    break;
                case "session_id":
                    sessionId = ulong.Parse(kv[1], CultureInfo.InvariantCulture);
                    break;
                case "error":
                    error = kv[1];
                    break;
            }
        }

        if (!success)
        {
            throw new InvalidOperationException($"Authentication failed: {error ?? response}");
        }

        return sessionId;
    }

    private static void SendSubscription(NetworkStream stream, Options options)
    {
        var builder = new StringBuilder();
        builder.Append("schema=").Append(options.Schema)
            .Append("|stype_in=").Append(options.StypeIn)
            .Append("|id=1");
        if (!string.IsNullOrEmpty(options.Start))
        {
            builder.Append("|start=").Append(options.Start);
        }

        builder.Append("|symbols=").Append(string.Join(',', options.Symbols))
            .Append("|snapshot=").Append(options.RequestSnapshot ? '1' : '0')
            .Append("|is_last=1\n");
        WriteCommand(stream, builder.ToString());
    }

    private static void WriteCommand(NetworkStream stream, string command)
    {
        var data = Encoding.ASCII.GetBytes(command);
        stream.Write(data, 0, data.Length);
    }

    private static string GenerateCramReply(string challenge, string apiKey)
    {
        var bucketId = apiKey[^5..];
        using var sha = SHA256.Create();
        var challengeBytes = Encoding.ASCII.GetBytes(challenge + '|' + apiKey);
        var hash = sha.ComputeHash(challengeBytes);
        var hex = Convert.ToHexString(hash).ToLowerInvariant();
        return hex + '-' + bucketId;
    }

    private static string ReadAsciiLine(NetworkStream stream)
    {
        var buffer = new List<byte>(128);
        while (true)
        {
            var value = stream.ReadByte();
            if (value < 0)
            {
                throw new IOException("Connection closed while waiting for line terminator.");
            }

            if (value == '\n')
            {
                break;
            }

            if (value != '\r')
            {
                buffer.Add((byte)value);
            }
        }

        return Encoding.ASCII.GetString(buffer.ToArray());
    }

    private static string ToIsoString(ulong unixNanos)
    {
        const long nanosPerTick = 100;
        var ticks = (long)(unixNanos / nanosPerTick);
        var dateTime = new DateTimeOffset(ticks, TimeSpan.Zero);
        return dateTime.ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss.fffffff'Z'", CultureInfo.InvariantCulture);
    }
}

internal sealed class Options
{
    public string? ApiKey { get; set; }
    public string Dataset { get; set; } = "GLBX.MDP3";
    public List<string> Symbols { get; } = new() { "ES.FUT" };
    public string Schema { get; set; } = "mbp-1";
    public string StypeIn { get; set; } = "parent";
    public bool RequestSnapshot { get; set; } = true;
    public string? Start { get; set; }
    public string? Gateway { get; set; }
    public int Port { get; set; } = 13000;
    public bool SendTsOut { get; set; }
    public int MaxRecords { get; set; } = 20;
    public bool Verbose { get; set; }
    public int? HeartbeatIntervalSeconds { get; set; }
    public string? ClientExtension { get; set; }

    public static Options Parse(string[] args)
    {
        var options = new Options();

        for (var i = 0; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--api-key":
                    options.ApiKey = ReadValue(args, ref i);
                    break;
                case "--dataset":
                    options.Dataset = ReadValue(args, ref i);
                    break;
                case "--symbols":
                    options.Symbols.Clear();
                    foreach (var symbol in ReadValue(args, ref i)
                                 .Split(',', StringSplitOptions.RemoveEmptyEntries)
                                 .Select(s => s.Trim())
                                 .Where(s => s.Length > 0))
                    {
                        options.Symbols.Add(symbol);
                    }

                    break;
                case "--schema":
                    options.Schema = ReadValue(args, ref i);
                    break;
                case "--stype-in":
                    options.StypeIn = ReadValue(args, ref i);
                    break;
                case "--no-snapshot":
                    options.RequestSnapshot = false;
                    break;
                case "--snapshot":
                    options.RequestSnapshot = true;
                    break;
                case "--start":
                    options.Start = ReadValue(args, ref i);
                    break;
                case "--gateway":
                    options.Gateway = ReadValue(args, ref i);
                    break;
                case "--port":
                    options.Port = int.Parse(ReadValue(args, ref i), CultureInfo.InvariantCulture);
                    break;
                case "--send-ts-out":
                    options.SendTsOut = true;
                    break;
                case "--max-records":
                    options.MaxRecords = int.Parse(ReadValue(args, ref i), CultureInfo.InvariantCulture);
                    break;
                case "--verbose":
                    options.Verbose = true;
                    break;
                case "--heartbeat":
                    options.HeartbeatIntervalSeconds = int.Parse(ReadValue(args, ref i), CultureInfo.InvariantCulture);
                    break;
                case "--client-extension":
                    options.ClientExtension = ReadValue(args, ref i);
                    break;
                case "--help":
                case "-h":
                    PrintUsage();
                    Environment.Exit(0);
                    break;
                default:
                    throw new ArgumentException($"Unknown argument '{args[i]}'. Use --help for usage information.");
            }
        }

        return options;
    }

    private static string ReadValue(string[] args, ref int index)
    {
        if (index + 1 >= args.Length)
        {
            throw new ArgumentException($"Expected a value after '{args[index]}'.");
        }

        index++;
        return args[index];
    }

    private static void PrintUsage()
    {
        Console.WriteLine(
            "Usage: dotnet run -- [options]\n" +
            "Options:\n" +
            "  --api-key <value>           Databento API key (or set DATABENTO_API_KEY).\n" +
            "  --dataset <code>            Dataset to subscribe to (default GLBX.MDP3).\n" +
            "  --symbols <list>            Comma-separated symbols to request (default ES.FUT).\n" +
            "  --schema <name>             Schema to request (default mbp-1).\n" +
            "  --stype-in <stype>          Input symbology type (default parent).\n" +
            "  --snapshot / --no-snapshot  Request an initial snapshot (default snapshot).\n" +
            "  --start <iso8601>           Optional historical replay start time.\n" +
            "  --gateway <host>            Override the live gateway hostname.\n" +
            "  --port <number>            Override the live gateway port (default 13000).\n" +
            "  --send-ts-out              Append gateway send timestamps to records.\n" +
            "  --heartbeat <seconds>      Request a custom heartbeat interval.\n" +
            "  --max-records <n>          Stop after printing N quotes (default 20, 0 = infinite).\n" +
            "  --verbose                  Print system/heartbeat messages.\n" +
            "  --client-extension <text>  Append custom text to the client user agent.\n");
    }
}

internal enum RType : byte
{
    Mbp0 = 0x00,
    Mbp1 = 0x01,
    Mbp10 = 0x0A,
    InstrumentDef = 0x13,
    Imbalance = 0x14,
    Error = 0x15,
    SymbolMapping = 0x16,
    System = 0x17,
}

internal enum SType : byte
{
    InstrumentId = 0,
    RawSymbol = 1,
    Smart = 2,
    Continuous = 3,
    Parent = 4,
    NasdaqSymbol = 5,
    CmsSymbol = 6,
    Isin = 7,
    UsCode = 8,
    BbgCompId = 9,
    BbgCompTicker = 10,
    Figi = 11,
    FigiTicker = 12,
}

internal readonly struct RecordHeader
{
    public RecordHeader(byte length, RType rType, ushort publisherId, uint instrumentId, ulong tsEvent)
    {
        Length = length;
        RType = rType;
        PublisherId = publisherId;
        InstrumentId = instrumentId;
        TsEvent = tsEvent;
    }

    public byte Length { get; }
    public RType RType { get; }
    public ushort PublisherId { get; }
    public uint InstrumentId { get; }
    public ulong TsEvent { get; }
    public int Size => Length * 4;
}

internal readonly struct DbnRecord
{
    public DbnRecord(RecordHeader header, byte[] payload)
    {
        Header = header;
        Payload = payload;
    }

    public RecordHeader Header { get; }
    public byte[] Payload { get; }
    public ReadOnlySpan<byte> PayloadSpan => Payload.AsSpan();
}

internal sealed class DbnRecordReader
{
    private readonly NetworkStream _stream;
    private readonly byte[] _headerBuffer = new byte[16];
    private bool _isCanceled;

    public DbnRecordReader(NetworkStream stream)
    {
        _stream = stream;
    }

    public bool IsCanceled => _isCanceled;

    public void Cancel() => _isCanceled = true;

    public DbnRecord? Read()
    {
        if (_isCanceled)
        {
            return null;
        }

        FillBuffer(_headerBuffer);
        var header = ParseHeader(_headerBuffer);
        var payloadLength = header.Size - _headerBuffer.Length;
        if (payloadLength < 0)
        {
            throw new InvalidOperationException($"Unexpected record size {header.Size}.");
        }

        var payload = new byte[payloadLength];
        if (payloadLength > 0)
        {
            FillBuffer(payload);
        }

        return new DbnRecord(header, payload);
    }

    private RecordHeader ParseHeader(ReadOnlySpan<byte> buffer)
    {
        var length = buffer[0];
        var rType = (RType)buffer[1];
        var publisherId = BinaryPrimitives.ReadUInt16LittleEndian(buffer.Slice(2, 2));
        var instrumentId = BinaryPrimitives.ReadUInt32LittleEndian(buffer.Slice(4, 4));
        var tsEvent = BinaryPrimitives.ReadUInt64LittleEndian(buffer.Slice(8, 8));
        return new RecordHeader(length, rType, publisherId, instrumentId, tsEvent);
    }

    private void FillBuffer(Span<byte> span)
    {
        var totalRead = 0;
        while (totalRead < span.Length)
        {
            var read = _stream.Read(span.Slice(totalRead));
            if (read == 0)
            {
                throw new IOException("Unexpected end of stream while reading DBN record.");
            }

            totalRead += read;
        }
    }
}

internal static class RecordDecoders
{
    public static Mbp1Quote ParseMbp1(ReadOnlySpan<byte> payload)
    {
        if (payload.Length < 64)
        {
            throw new ArgumentException("MBP-1 payload must be 64 bytes.", nameof(payload));
        }

        var offset = 0;
        var price = BinaryPrimitives.ReadInt64LittleEndian(payload.Slice(offset, 8));
        offset += 8;
        var size = BinaryPrimitives.ReadUInt32LittleEndian(payload.Slice(offset, 4));
        offset += 4;
        var action = (char)payload[offset++];
        var side = (char)payload[offset++];
        var flags = payload[offset++];
        var depth = payload[offset++];
        var tsRecv = BinaryPrimitives.ReadUInt64LittleEndian(payload.Slice(offset, 8));
        offset += 8;
        var tsInDelta = BinaryPrimitives.ReadInt32LittleEndian(payload.Slice(offset, 4));
        offset += 4;
        var sequence = BinaryPrimitives.ReadUInt32LittleEndian(payload.Slice(offset, 4));
        offset += 4;
        var bidPx = BinaryPrimitives.ReadInt64LittleEndian(payload.Slice(offset, 8));
        offset += 8;
        var askPx = BinaryPrimitives.ReadInt64LittleEndian(payload.Slice(offset, 8));
        offset += 8;
        var bidSize = BinaryPrimitives.ReadUInt32LittleEndian(payload.Slice(offset, 4));
        offset += 4;
        var askSize = BinaryPrimitives.ReadUInt32LittleEndian(payload.Slice(offset, 4));
        offset += 4;
        var bidCount = BinaryPrimitives.ReadUInt32LittleEndian(payload.Slice(offset, 4));
        offset += 4;
        var askCount = BinaryPrimitives.ReadUInt32LittleEndian(payload.Slice(offset, 4));

        return new Mbp1Quote(price, size, action, side, flags, depth, tsRecv, tsInDelta, sequence,
            bidPx, askPx, bidSize, askSize, bidCount, askCount);
    }

    public static string ParseInstrumentDefinition(ReadOnlySpan<byte> payload)
    {
        if (payload.Length < 504)
        {
            return string.Empty;
        }

        const int rawSymbolOffset = 222;
        const int rawSymbolLength = 71;
        var rawSymbolSpan = payload.Slice(rawSymbolOffset, rawSymbolLength);
        var terminator = rawSymbolSpan.IndexOf((byte)0);
        if (terminator < 0)
        {
            terminator = rawSymbolSpan.Length;
        }

        return Encoding.ASCII.GetString(rawSymbolSpan.Slice(0, terminator));
    }

    public static SymbolMappingMessage? ParseSymbolMapping(ReadOnlySpan<byte> payload)
    {
        if (payload.Length < 160)
        {
            return null;
        }

        var offset = 0;
        var stypeIn = (SType)payload[offset++];
        var stypeInSymbol = ReadSymbol(payload.Slice(offset, 71));
        offset += 71;
        var stypeOut = (SType)payload[offset++];
        var stypeOutSymbol = ReadSymbol(payload.Slice(offset, 71));
        offset += 71;
        offset += 16; // skip start_ts and end_ts

        return new SymbolMappingMessage(stypeIn, stypeInSymbol, stypeOut, stypeOutSymbol);
    }

    public static string ParseError(ReadOnlySpan<byte> payload)
    {
        var span = payload.Slice(0, Math.Min(302, payload.Length));
        var terminator = span.IndexOf((byte)0);
        if (terminator < 0)
        {
            terminator = span.Length;
        }

        return Encoding.ASCII.GetString(span.Slice(0, terminator));
    }

    public static SystemMessage ParseSystem(ReadOnlySpan<byte> payload)
    {
        var messageRegion = payload.Slice(0, Math.Min(303, payload.Length));
        var terminator = messageRegion.IndexOf((byte)0);
        if (terminator < 0)
        {
            terminator = messageRegion.Length;
        }

        var message = Encoding.ASCII.GetString(messageRegion.Slice(0, terminator));
        var code = payload.Length > 303 ? payload[303] : byte.MaxValue;
        var isHeartbeat = message.StartsWith("Heartbeat", StringComparison.OrdinalIgnoreCase) || code == 0x01;
        return new SystemMessage(message, isHeartbeat);
    }

    private static string ReadSymbol(ReadOnlySpan<byte> span)
    {
        var terminator = span.IndexOf((byte)0);
        if (terminator < 0)
        {
            terminator = span.Length;
        }

        return Encoding.ASCII.GetString(span.Slice(0, terminator));
    }
}

internal readonly record struct Mbp1Quote(
    long Price,
    uint Size,
    char Action,
    char Side,
    byte Flags,
    byte Depth,
    ulong TsRecv,
    int TsInDelta,
    uint Sequence,
    long BestBid,
    long BestAsk,
    uint BestBidSize,
    uint BestAskSize,
    uint BestBidCount,
    uint BestAskCount);

internal readonly record struct SymbolMappingMessage(
    SType InputType,
    string InputSymbol,
    SType OutputType,
    string OutputSymbol);

internal readonly record struct SystemMessage(string Message, bool IsHeartbeat);

internal sealed class DbnMetadataReader
{
    public static DbnMetadata Read(NetworkStream stream)
    {
        Span<byte> prelude = stackalloc byte[8];
        Fill(stream, prelude);
        if (prelude[0] != (byte)'D' || prelude[1] != (byte)'B' || prelude[2] != (byte)'N')
        {
            throw new InvalidOperationException("Live gateway metadata did not start with DBN prefix.");
        }

        var version = prelude[3];
        var length = BinaryPrimitives.ReadUInt32LittleEndian(prelude.Slice(4, 4));
        var metadataBuffer = new byte[length];
        Fill(stream, metadataBuffer);
        return ParseMetadata(metadataBuffer, version);
    }

    private static DbnMetadata ParseMetadata(ReadOnlySpan<byte> buffer, byte version)
    {
        var cursor = 0;
        var dataset = ReadCString(buffer, ref cursor, 16);
        var rawSchema = BinaryPrimitives.ReadUInt16LittleEndian(buffer.Slice(cursor, 2));
        cursor += 2;
        Schema? schema = rawSchema == ushort.MaxValue ? null : (Schema)rawSchema;
        var start = BinaryPrimitives.ReadUInt64LittleEndian(buffer.Slice(cursor, 8));
        cursor += 8;
        var end = BinaryPrimitives.ReadUInt64LittleEndian(buffer.Slice(cursor, 8));
        cursor += 8;
        var limit = BinaryPrimitives.ReadUInt64LittleEndian(buffer.Slice(cursor, 8));
        cursor += 8;
        if (version == 1)
        {
            cursor += 8; // record_count
        }

        var rawStypeIn = buffer[cursor++];
        SType? stypeIn = rawStypeIn == byte.MaxValue ? null : (SType)rawStypeIn;
        var stypeOut = (SType)buffer[cursor++];
        var tsOut = buffer[cursor++] != 0;
        ushort symbolLen;
        if (version > 1)
        {
            symbolLen = BinaryPrimitives.ReadUInt16LittleEndian(buffer.Slice(cursor, 2));
            cursor += 2;
        }
        else
        {
            symbolLen = 71;
        }

        cursor += version == 1 ? 47 : 53; // reserved

        var schemaDefinitionLength = BinaryPrimitives.ReadUInt32LittleEndian(buffer.Slice(cursor, 4));
        cursor += 4;
        if (schemaDefinitionLength != 0)
        {
            cursor += checked((int)schemaDefinitionLength);
        }

        var symbols = ReadSymbolList(buffer, ref cursor, symbolLen);
        var partial = ReadSymbolList(buffer, ref cursor, symbolLen);
        var notFound = ReadSymbolList(buffer, ref cursor, symbolLen);
        var mappings = ReadSymbolMappings(buffer, ref cursor, symbolLen);

        return new DbnMetadata(version, dataset, schema, start, end, limit, stypeIn, stypeOut,
            tsOut, symbolLen, symbols, partial, notFound, mappings);
    }

    private static IReadOnlyDictionary<string, IReadOnlyList<SymbolInterval>> ReadSymbolMappings(
        ReadOnlySpan<byte> buffer,
        ref int cursor,
        int symbolLen)
    {
        var count = BinaryPrimitives.ReadUInt32LittleEndian(buffer.Slice(cursor, 4));
        cursor += 4;
        var mappings = new Dictionary<string, IReadOnlyList<SymbolInterval>>((int)count);
        for (var i = 0; i < count; i++)
        {
            var rawSymbol = ReadCString(buffer, ref cursor, symbolLen);
            var intervalCount = BinaryPrimitives.ReadUInt32LittleEndian(buffer.Slice(cursor, 4));
            cursor += 4;
            var intervals = new List<SymbolInterval>((int)intervalCount);
            for (var j = 0; j < intervalCount; j++)
            {
                var start = BinaryPrimitives.ReadUInt32LittleEndian(buffer.Slice(cursor, 4));
                cursor += 4;
                var end = BinaryPrimitives.ReadUInt32LittleEndian(buffer.Slice(cursor, 4));
                cursor += 4;
                var symbol = ReadCString(buffer, ref cursor, symbolLen);
                intervals.Add(new SymbolInterval(start, end, symbol));
            }

            mappings[rawSymbol] = intervals;
        }

        return mappings;
    }

    private static IReadOnlyList<string> ReadSymbolList(ReadOnlySpan<byte> buffer, ref int cursor, int symbolLen)
    {
        var count = BinaryPrimitives.ReadUInt32LittleEndian(buffer.Slice(cursor, 4));
        cursor += 4;
        var list = new List<string>((int)count);
        for (var i = 0; i < count; i++)
        {
            list.Add(ReadCString(buffer, ref cursor, symbolLen));
        }

        return list;
    }

    private static string ReadCString(ReadOnlySpan<byte> buffer, ref int cursor, int length)
    {
        var span = buffer.Slice(cursor, length);
        var terminator = span.IndexOf((byte)0);
        if (terminator < 0)
        {
            terminator = span.Length;
        }

        cursor += length;
        return Encoding.ASCII.GetString(span.Slice(0, terminator));
    }

    private static void Fill(NetworkStream stream, Span<byte> destination)
    {
        var read = 0;
        while (read < destination.Length)
        {
            var n = stream.Read(destination.Slice(read));
            if (n == 0)
            {
                throw new IOException("Unexpected end of stream while reading metadata.");
            }

            read += n;
        }
    }

    private static void Fill(NetworkStream stream, byte[] destination)
    {
        Fill(stream, destination.AsSpan());
    }
}

internal readonly record struct SymbolInterval(uint StartDate, uint EndDate, string Symbol);

internal enum Schema : ushort
{
    Mbo = 0,
    Mbp1 = 1,
    Mbp10 = 2,
    Tbbo = 3,
    Trades = 4,
    Ohlcv1S = 5,
    Ohlcv1M = 6,
    Ohlcv1H = 7,
    Ohlcv1D = 8,
    Definition = 9,
    Statistics = 10,
    Imbalance = 11,
}

internal sealed record DbnMetadata(
    byte Version,
    string Dataset,
    Schema? Schema,
    ulong Start,
    ulong End,
    ulong Limit,
    SType? StypeIn,
    SType StypeOut,
    bool TsOut,
    int SymbolCStringLength,
    IReadOnlyList<string> Symbols,
    IReadOnlyList<string> Partial,
    IReadOnlyList<string> NotFound,
    IReadOnlyDictionary<string, IReadOnlyList<SymbolInterval>> Mappings);
