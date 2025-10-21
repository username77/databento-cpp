# Databento C++/CLI Wrapper

This directory contains a Visual Studio solution that exposes a minimal .NET interface for the native `databento.lib`. The wrapper is implemented with C++/CLI so it can be consumed by .NET 8 and .NET 9 applications.

## Layout

- `DatabentoWrapper.sln` – Visual Studio 2022 solution that builds the wrapper and a simple C# console application.
- `DatabentoWrapper/` – C++/CLI project that links against `databento.lib` and exports the managed API.
- `DemoApp/` – Multi-targeted (.NET 8/9) console app demonstrating how to call the wrapper from C#.
- `Databento.props` – MSBuild property sheet that wires include/library directories for the native SDK and its third-party dependencies.

## Prerequisites

1. Build the native Databento SDK for Windows so that `databento.lib` is available under `build/<Configuration>` (for example, `build/Release`).
2. Install Visual Studio 2022 with the following workloads:
   - Desktop development with C++ (C++/CLI support is required).
   - .NET desktop development (for building the C# demo app).
3. Install OpenSSL and zstd. The property sheet assumes they come from vcpkg and uses the `VCPKG_INSTALLED_DIR` + `VcpkgTriplet` layout by default. You can override the locations in `Databento.props` if needed.

## Building the wrapper

1. Open `DatabentoWrapper.sln` in Visual Studio.
2. Confirm that the `DatabentoIncludeDir`, `DatabentoLibDir`, `OpenSslLibDir`, and `ZstdLibDir` entries in `Databento.props` point to valid locations. Override them in your user property sheet if your layout is different.
3. Select the desired configuration (`Debug` or `Release`) and the `x64` platform.
4. Build the **DatabentoWrapper** project. The output DLL is placed under `DatabentoWrapper\bin\<Configuration>\<TargetFramework>\`.

## Using the wrapper from C#

The managed API currently exposes a `LiveClient` that allows you to subscribe to live trades and stream records through a managed delegate. The demo application (`DemoApp`) shows the basic usage pattern:

```csharp
using var client = new LiveClient(apiKey, "GLBX.MDP3");
client.SubscribeTrades(new[] { "ESZ5", "ESZ5 C6000" }, SymbolType.RawSymbol);
client.Start((kind, instrumentId, symbol, text) =>
{
    Console.WriteLine($"{kind}: {instrumentId} {symbol} {text}");
});
// Run until you are ready to stop.
client.Stop();
```

After calling `Stop` the underlying native client is disposed; create a new `LiveClient` instance or resubscribe before restarting the stream.

## Customisation notes

- Both projects target `net8.0-windows` and `net9.0-windows`. Pick the framework you need when building or publishing the managed application.
- The wrapper links against both `ssl.lib`/`crypto.lib` and the prefixed `libssl.lib`/`libcrypto.lib` variants to accommodate different vcpkg configurations. Remove the redundant names if they are not required in your environment.
- Add additional exports or schemas by extending `DatabentoWrapper.cpp` and rebuilding the DLL.
