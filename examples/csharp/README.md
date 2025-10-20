# C# Examples

## Quote downloader sample

The [`QuotesSample`](./QuotesSample) console application shows how to pull a
small slice of best bid and offer quotes from Databento's historical API using
`timeseries.get_range`.

### Prerequisites

* [.NET 8 SDK](https://dotnet.microsoft.com/en-us/download)
* A Databento API key with access to the dataset you want to query

### Running the sample

```bash
cd examples/csharp/QuotesSample
dotnet run -- \
  --api-key=db-your-key-here \
  --dataset=GLBX.MDP3 \
  --symbol=ES.FUT \
  --start=2024-05-01T13:30:00Z \
  --end=2024-05-01T13:31:00Z \
  --limit=50
```

The sample requests market-by-price level 1 (`schema=mbp-1`) data which
corresponds to consolidated quotes for the given instrument. The parameters map
directly onto the fields used by the C++ historical client: dataset, schema,
symbol list, time range, symbol types, encoding, compression, and record limit
are all posted to the `/v0/timeseries.get_range` endpoint.【F:src/historical.cpp†L784-L848】【F:src/enums.cpp†L10-L32】【F:src/enums.cpp†L450-L480】

You can omit `--api-key` and rely on the `DATABENTO_API_KEY` environment
variable if you prefer.

The sample prints the first few CSV lines returned by the API. You can adjust
`--start`, `--end`, or `--limit` to control the size of the response, or supply
`--schema`, `--stype-in`, and `--stype-out` to request different quote or trade
formats. Setting `--encoding=json` or `--compression=zstd` is also possible if
you prefer other formats and handle the decoding yourself.

## Live quote subscriber sample

[`LiveQuotesSample`](./LiveQuotesSample) shows how to connect to the Databento
live subscription gateway over TCP, authenticate with the CRAM challenge, send
an MBP-1 subscription, and decode the resulting DBN metadata and quote
messages.【F:examples/csharp/LiveQuotesSample/Program.cs†L15-L206】【F:examples/csharp/LiveQuotesSample/Program.cs†L594-L821】

### Running the sample

```bash
cd examples/csharp/LiveQuotesSample
dotnet run -- \
  --api-key=db-your-key-here \
  --dataset=GLBX.MDP3 \
  --symbols=ES.FUT \
  --schema=mbp-1 \
  --stype-in=parent \
  --max-records=20
```

The sample performs the following steps:

1. Derives the TCP gateway hostname from the dataset and establishes a socket
   connection on port 13000.【F:examples/csharp/LiveQuotesSample/Program.cs†L45-L64】
2. Reads the gateway greeting, hashes the CRAM challenge together with the API
   key, and posts the authentication request with optional heartbeat
   configuration.【F:examples/csharp/LiveQuotesSample/Program.cs†L108-L171】
3. Writes a `schema=...|symbols=...` subscription message, starts the session,
   and decodes the DBN metadata frame to learn the output symbology length and
   requested symbols.【F:examples/csharp/LiveQuotesSample/Program.cs†L73-L105】【F:examples/csharp/LiveQuotesSample/Program.cs†L201-L283】
4. Streams DBN records, caching instrument definitions to resolve
   `instrument_id` into text symbols and printing each MBP-1 top-of-book update
   with bid/ask prices scaled by `1e9`.【F:examples/csharp/LiveQuotesSample/Program.cs†L84-L154】【F:examples/csharp/LiveQuotesSample/Program.cs†L438-L666】

Additional flags let you request snapshots (`--snapshot/--no-snapshot`), replay
from a historical start time (`--start`), change the input symbology type,
extend the user agent string, or adjust the number of printed quote updates.
Environment variable `DATABENTO_API_KEY` is used automatically when `--api-key`
is omitted.【F:examples/csharp/LiveQuotesSample/Program.cs†L18-L31】【F:examples/csharp/LiveQuotesSample/Program.cs†L343-L416】
