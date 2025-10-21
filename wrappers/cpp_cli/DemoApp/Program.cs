using System;
using System.Threading;
using DatabentoWrapper;

namespace DemoApp
{
    internal static class Program
    {
        private static void Main()
        {
            var apiKey = Environment.GetEnvironmentVariable("DATABENTO_API_KEY") ?? "db-REPLACE";
            const string dataset = "GLBX.MDP3";

            using var client = new LiveClient(apiKey, dataset);
            client.SubscribeTrades(new[] { "ESZ5", "ESZ5 C6000" }, SymbolType.RawSymbol);

            client.Start((kind, instrumentId, symbol, text) =>
            {
                Console.WriteLine($"{kind}: {instrumentId} {symbol} {text}");
            });

            Thread.Sleep(TimeSpan.FromSeconds(10));
            client.Stop();
        }
    }
}
