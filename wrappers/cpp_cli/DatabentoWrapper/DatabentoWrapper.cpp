#include "pch.h"
#include "DatabentoWrapper.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <databento/live.hpp>
#include <databento/live_threaded.hpp>
#include <databento/record.hpp>
#include <databento/symbol_map.hpp>

using msclr::interop::marshal_as;

namespace DatabentoWrapper
{
    namespace
    {
        inline std::string ToStdString(String^ value)
        {
            return marshal_as<std::string>(value);
        }

        inline String^ ToManagedString(const std::string& value)
        {
            return value.empty() ? String::Empty : gcnew String(value.c_str());
        }

        databento::SType ToNativeSymbolType(SymbolType type)
        {
            switch (type)
            {
            case SymbolType::InstrumentId:
                return databento::SType::InstrumentId;
            case SymbolType::Parent:
                return databento::SType::Parent;
            case SymbolType::RawSymbol:
            default:
                return databento::SType::RawSymbol;
            }
        }

        inline String^ KindLiteral(const char* value)
        {
            return gcnew String(value);
        }
    }

    struct LiveClient::NativeState
    {
        NativeState(std::string key, std::string dataset)
            : api_key(std::move(key)), dataset(std::move(dataset))
        {
        }

        std::string api_key;
        std::string dataset;
        std::unique_ptr<databento::LiveThreaded> client;
        databento::PitSymbolMap symbol_map{};
        gcroot<RecordCallback^> callback;
        std::atomic<bool> running{false};
        std::atomic<bool> stop_requested{false};
    };

    LiveClient::LiveClient(String^ apiKey, String^ dataset)
        : _impl(nullptr)
    {
        if (String::IsNullOrWhiteSpace(apiKey))
        {
            throw gcnew ArgumentException("API key cannot be null or whitespace.", "apiKey");
        }
        if (String::IsNullOrWhiteSpace(dataset))
        {
            throw gcnew ArgumentException("Dataset cannot be null or whitespace.", "dataset");
        }

        _impl = new NativeState(ToStdString(apiKey), ToStdString(dataset));
    }

    LiveClient::~LiveClient()
    {
        this->!LiveClient();
    }

    LiveClient::!LiveClient()
    {
        if (_impl == nullptr)
        {
            return;
        }

        try
        {
            Stop();
        }
        catch (...)
        {
            // Suppress all exceptions during finalization.
        }

        delete _impl;
        _impl = nullptr;
    }

    void LiveClient::ThrowIfDisposed()
    {
        if (_impl == nullptr)
        {
            throw gcnew ObjectDisposedException(LiveClient::typeid->FullName);
        }
    }

    void LiveClient::ValidateSymbols(array<String^>^ symbols)
    {
        if (symbols == nullptr)
        {
            throw gcnew ArgumentNullException("symbols");
        }
        if (symbols->Length == 0)
        {
            throw gcnew ArgumentException("At least one symbol is required.", "symbols");
        }
        for each (String ^ symbol in symbols)
        {
            if (String::IsNullOrWhiteSpace(symbol))
            {
                throw gcnew ArgumentException("Symbols cannot contain null or whitespace entries.", "symbols");
            }
        }
    }

    void LiveClient::EnsureClient()
    {
        ThrowIfDisposed();
        auto& state = *_impl;
        if (state.client)
        {
            return;
        }

        try
        {
            auto builder = databento::LiveThreaded::Builder();
            builder.SetKey(state.api_key);
            builder.SetDataset(state.dataset);
            auto client = builder.BuildThreaded();
            state.client = std::make_unique<databento::LiveThreaded>(std::move(client));
        }
        catch (const std::exception& ex)
        {
            throw gcnew InvalidOperationException(gcnew String(ex.what()));
        }
        catch (...)
        {
            throw gcnew InvalidOperationException("Failed to create Databento live client.");
        }
    }

    void LiveClient::SubscribeTrades(array<String^>^ symbols, SymbolType symbolType)
    {
        ValidateSymbols(symbols);
        EnsureClient();

        auto& state = *_impl;
        std::vector<std::string> nativeSymbols;
        nativeSymbols.reserve(static_cast<std::size_t>(symbols->Length));
        for each (String ^ symbol in symbols)
        {
            nativeSymbols.emplace_back(ToStdString(symbol));
        }

        try
        {
            state.client->Subscribe(nativeSymbols, databento::Schema::Trades, ToNativeSymbolType(symbolType));
        }
        catch (const std::exception& ex)
        {
            throw gcnew InvalidOperationException(gcnew String(ex.what()));
        }
        catch (...)
        {
            throw gcnew InvalidOperationException("Failed to subscribe to trades.");
        }
    }

    void LiveClient::Start(RecordCallback^ callback)
    {
        ThrowIfDisposed();
        if (callback == nullptr)
        {
            throw gcnew ArgumentNullException("callback");
        }

        EnsureClient();
        auto& state = *_impl;

        bool expected = false;
        if (!state.running.compare_exchange_strong(expected, true))
        {
            throw gcnew InvalidOperationException("The live client is already running.");
        }

        state.stop_requested.store(false, std::memory_order_relaxed);
        state.callback = callback;
        state.symbol_map = databento::PitSymbolMap{};

        auto* nativeState = _impl;
        auto recordHandler = [nativeState](const databento::Record& record) -> databento::KeepGoing
        {
            if (nativeState->stop_requested.load(std::memory_order_relaxed))
            {
                nativeState->running.store(false, std::memory_order_relaxed);
                return databento::KeepGoing::Stop;
            }

            RecordCallback^ cb = nativeState->callback;
            if (cb == nullptr)
            {
                nativeState->running.store(false, std::memory_order_relaxed);
                return databento::KeepGoing::Stop;
            }

            auto send = [&](String^ kind, std::uint32_t instrument, const std::string& symbol, const std::string& text)
            {
                try
                {
                    cb->Invoke(kind, instrument, ToManagedString(symbol), ToManagedString(text));
                }
                catch (...)
                {
                    // Swallow exceptions thrown by managed callbacks to keep the feed alive.
                }
            };

            try
            {
                if (const auto* mapping = record.GetIf<databento::SymbolMappingMsg>())
                {
                    nativeState->symbol_map.OnSymbolMapping(*mapping);
                    send(KindLiteral("mapping"), 0U, std::string{}, databento::ToString(*mapping));
                }
                else if (const auto* trade = record.GetIf<databento::TradeMsg>())
                {
                    const auto instrument = trade->hd.instrument_id;
                    std::string symbol;
                    try
                    {
                        symbol = nativeState->symbol_map.At(*trade);
                    }
                    catch (...)
                    {
                        symbol.clear();
                    }
                    send(KindLiteral("trade"), instrument, symbol, databento::ToString(*trade));
                }
                else if (const auto* system = record.GetIf<databento::SystemMsg>())
                {
                    if (!system->IsHeartbeat())
                    {
                        send(KindLiteral("system"), 0U, std::string{}, databento::ToString(*system));
                    }
                }
                else if (const auto* error = record.GetIf<databento::ErrorMsg>())
                {
                    send(KindLiteral("error"), 0U, std::string{}, databento::ToString(*error));
                }
                else
                {
                    std::ostringstream oss;
                    oss << "Unhandled record rtype=0x" << std::hex << record.RType();
                    send(KindLiteral("unknown"), 0U, std::string{}, oss.str());
                }
            }
            catch (...)
            {
                // Continue processing subsequent records even if parsing fails.
            }

            if (nativeState->stop_requested.load(std::memory_order_relaxed))
            {
                nativeState->running.store(false, std::memory_order_relaxed);
                return databento::KeepGoing::Stop;
            }

            return databento::KeepGoing::Continue;
        };

        auto exceptionHandler = [nativeState](const std::exception&)
        {
            nativeState->running.store(false, std::memory_order_relaxed);
            return databento::LiveThreaded::ExceptionAction::Stop;
        };

        try
        {
            state.client->Start({}, std::move(recordHandler), std::move(exceptionHandler));
        }
        catch (const std::exception& ex)
        {
            state.running.store(false, std::memory_order_relaxed);
            throw gcnew InvalidOperationException(gcnew String(ex.what()));
        }
        catch (...)
        {
            state.running.store(false, std::memory_order_relaxed);
            throw gcnew InvalidOperationException("Failed to start live stream.");
        }
    }

    void LiveClient::Stop()
    {
        if (_impl == nullptr)
        {
            return;
        }

        auto& state = *_impl;
        state.stop_requested.store(true, std::memory_order_relaxed);

        if (state.client)
        {
            try
            {
                state.client->BlockForStop(std::chrono::milliseconds(0));
            }
            catch (...)
            {
                // Ignore errors while stopping.
            }
            state.client.reset();
        }

        state.running.store(false, std::memory_order_relaxed);
        state.callback = nullptr;
        state.symbol_map = databento::PitSymbolMap{};
        state.stop_requested.store(false, std::memory_order_relaxed);
    }
}
