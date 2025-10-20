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
