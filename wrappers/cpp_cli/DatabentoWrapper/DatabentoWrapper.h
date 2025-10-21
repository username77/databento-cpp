#pragma once

using namespace System;

namespace DatabentoWrapper
{
    public delegate void RecordCallback(String^ kind, UInt32 instrumentId, String^ symbol, String^ text);

    public enum class SymbolType : UInt32
    {
        InstrumentId = 0,
        RawSymbol = 1,
        Parent = 4,
    };

    public ref class LiveClient sealed
    {
    public:
        LiveClient(String^ apiKey, String^ dataset);
        ~LiveClient();
        !LiveClient();

        void SubscribeTrades(array<String^>^ symbols, SymbolType symbolType);
        void SubscribeTrades(array<String^>^ symbols)
        {
            SubscribeTrades(symbols, SymbolType::RawSymbol);
        }

        void Start(RecordCallback^ callback);
        void Stop();

    private:
        void EnsureClient();
        void ThrowIfDisposed();
        static void ValidateSymbols(array<String^>^ symbols);

        struct NativeState;
        NativeState* _impl;
    };
}
