using System;
using System.Collections.Generic;
using System.Linq;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text;

namespace QuotesSample;

internal static class Program
{
    private const string Endpoint = "https://hist.databento.com/v0/timeseries.get_range";
    private const string DefaultDataset = "GLBX.MDP3";
    private const string DefaultSymbol = "ES.FUT";
    private const string DefaultStart = "2024-05-01T13:30:00Z";
    private const string DefaultEnd = "2024-05-01T13:31:00Z";
    private const string DefaultLimit = "50";

    private static async Task<int> Main(string[] args)
    {
        var apiKey = Environment.GetEnvironmentVariable("DATABENTO_API_KEY");
        if (string.IsNullOrWhiteSpace(apiKey))
        {
            Console.Error.WriteLine("Set the DATABENTO_API_KEY environment variable to your Databento API key.");
            return 1;
        }

        var options = ParseArgs(args);
        var dataset = options.GetValueOrDefault("dataset", DefaultDataset);
        var symbol = options.GetValueOrDefault("symbol", DefaultSymbol);
        var start = options.GetValueOrDefault("start", DefaultStart);
        var end = options.GetValueOrDefault("end", DefaultEnd);
        var limit = options.GetValueOrDefault("limit", DefaultLimit);

        using var httpClient = new HttpClient();
        httpClient.DefaultRequestHeaders.Authorization = BuildBasicAuthHeader(apiKey);
        httpClient.DefaultRequestHeaders.Accept.ParseAdd("text/csv");
        httpClient.DefaultRequestHeaders.UserAgent.ParseAdd("databento-csharp-sample/1.0");

        var formValues = new Dictionary<string, string>
        {
            ["dataset"] = dataset,
            ["symbols"] = symbol,
            ["schema"] = "mbp-1",
            ["stype_in"] = "parent",
            ["stype_out"] = "instrument_id",
            ["start"] = start,
            ["end"] = end,
            ["encoding"] = "csv",
            ["compression"] = "none",
            ["limit"] = limit,
        };

        Console.WriteLine($"Requesting quotes for {symbol} from {dataset} between {start} and {end}...");

        using var response = await httpClient.PostAsync(Endpoint, new FormUrlEncodedContent(formValues));
        if (!response.IsSuccessStatusCode)
        {
            Console.Error.WriteLine($"Request failed with status {(int)response.StatusCode} {response.ReasonPhrase}.");
            var errorBody = await response.Content.ReadAsStringAsync();
            if (!string.IsNullOrWhiteSpace(errorBody))
            {
                Console.Error.WriteLine(errorBody);
            }
            return 1;
        }

        var csvPayload = await response.Content.ReadAsStringAsync();
        Console.WriteLine("First few CSV lines returned by the API:");
        foreach (var line in csvPayload
                     .Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries)
                     .Take(10))
        {
            Console.WriteLine(line);
        }

        Console.WriteLine();
        Console.WriteLine("Adjust --start/--end or --limit to control the time window and number of quote updates.");
        return 0;
    }

    private static AuthenticationHeaderValue BuildBasicAuthHeader(string apiKey)
    {
        var token = Convert.ToBase64String(Encoding.ASCII.GetBytes(apiKey + ":"));
        return new AuthenticationHeaderValue("Basic", token);
    }

    private static Dictionary<string, string> ParseArgs(string[] args)
    {
        var results = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var arg in args)
        {
            if (!arg.StartsWith("--", StringComparison.Ordinal))
            {
                continue;
            }

            var split = arg[2..].Split('=', 2, StringSplitOptions.TrimEntries);
            if (split.Length == 2 && !string.IsNullOrEmpty(split[0]) && !string.IsNullOrEmpty(split[1]))
            {
                results[split[0]] = split[1];
            }
        }

        return results;
    }
}
