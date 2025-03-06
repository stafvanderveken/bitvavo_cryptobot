#include "API_Handling.h"
#include "config.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>

using namespace std;

// **CryptoTradingBot class definition**
class CryptoTradingBot {
private:
    string market;
    string cryptoAsset;
    string fiatAsset;
    double entryPrice;
    double totalProfitLoss = 0.0;
    double boughtCryptoAmount = 0.0;
    map<string, vector<vector<string>>> candlesByInterval; // Key: interval, Value: candles
    map<string, long long> lastTimestamps;                 // Key: interval, Value: last fetched timestamp
    vector<string> intervals = { "1m", "5m", "15m", "1h" };  // Fixed intervals to fetch
    map<string, long long> lastSavedTimestamps;            // Key: interval, Value: last saved timestamp
    double maxPositionSize = 0.25;
    bool isSimulation;
    double simFiatBalance;
    double simCryptoBalance;
    string tradeLogFile;
    string profitLogFile;
    chrono::steady_clock::time_point lastSaveTime = chrono::steady_clock::now();
    int saveIntervalMinutes = 10; // Save every 10 minutes

    // **Indicator data structures**
    struct IndicatorData {
        double rsi = 0.0;
        double macd = 0.0;
        double macd_signal = 0.0;
        double macd_hist = 0.0;
        double ema = 0.0;
        double bb_middle = 0.0;
        double bb_upper = 0.0;
        double bb_lower = 0.0;
        double atr = 0.0;
    };
    map<string, vector<IndicatorData>> indicatorsByInterval; // Key: interval, Value: indicators

    // **Load total profit/loss from file**
    double loadTotalProfitLoss() {
        double profit = 0.0;
        ifstream file(profitLogFile);
        if (file.is_open()) {
            file >> profit;
            file.close();
        }
        return profit;
    }

    // **Save total profit/loss to file**
    void saveTotalProfitLoss(double profit) {
        ofstream file(profitLogFile, ios::trunc);
        if (file.is_open()) {
            file << profit;
            file.close();
        }
    }

    // **Log trades to file**
    void logTrade(const string& tradeType, double amount, double price, double profitLoss = 0.0) {
        time_t now = time(nullptr);
        char buf[80];
        tm localTime;
        localtime_s(&localTime, &now);
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &localTime);
        string mode = isSimulation ? "[SIMULATION]" : "[REAL]";
        ofstream logFile(tradeLogFile, ios::app);
        if (logFile.is_open()) {
            logFile << mode << " [" << buf << "] " << tradeType
                << " | Amount: " << amount
                << " | Price: " << price;
            if (tradeType == "SELL") {
                logFile << " | Profit/Loss: " << profitLoss
                    << " | Total Profit/Loss: " << totalProfitLoss;
            }
            logFile << "\n";
            logFile.close();
        }
        else {
            cerr << "Unable to open " << tradeLogFile << " for writing." << endl;
        }
    }

    // **Calculate Exponential Moving Average (EMA)**
    double calculateEMA(const vector<double>& prices, int period, int index, double prevEMA) {
        double multiplier = 2.0 / (period + 1.0);
        if (index == 0) return prices[0];
        return prices[index] * multiplier + prevEMA * (1.0 - multiplier);
    }

    // **Calculate indicators for a given interval**
    void calculateIndicators(const string& interval) {
        auto candleIt = candlesByInterval.find(interval);
        if (candleIt == candlesByInterval.end() || candleIt->second.empty()) return;

        vector<vector<string>>& candles = candleIt->second;
        vector<double> closes, highs, lows;
        for (const auto& candle : candles) {
            closes.push_back(stod(candle[4])); // Close
            highs.push_back(stod(candle[2]));  // High
            lows.push_back(stod(candle[3]));   // Low
        }
        vector<IndicatorData>& indicators = indicatorsByInterval[interval];
        indicators.resize(closes.size());

        // RSI (14 period)
        if (closes.size() >= 14) {
            vector<double> delta(closes.size(), 0.0);
            vector<double> gain(closes.size(), 0.0);
            vector<double> loss(closes.size(), 0.0);
            for (size_t i = 1; i < closes.size(); i++) {
                delta[i] = closes[i] - closes[i - 1];
                if (delta[i] > 0) gain[i] = delta[i];
                else loss[i] = -delta[i];
            }
            double avgGain = 0.0, avgLoss = 0.0;
            for (int i = 13; i < 14; i++) {
                avgGain += gain[i];
                avgLoss += loss[i];
            }
            avgGain /= 14.0;
            avgLoss /= 14.0;
            for (size_t i = 14; i < closes.size(); i++) {
                avgGain = (avgGain * 13 + gain[i]) / 14.0;
                avgLoss = (avgLoss * 13 + loss[i]) / 14.0;
                double rs = (avgLoss == 0) ? 100 : avgGain / avgLoss;
                indicators[i].rsi = 100 - (100 / (1 + rs));
            }
        }

        // MACD (12, 26, 9)
        vector<double> ema12(closes.size(), 0.0), ema26(closes.size(), 0.0);
        for (size_t i = 0; i < closes.size(); i++) {
            ema12[i] = calculateEMA(closes, 12, i, (i > 0) ? ema12[i - 1] : closes[0]);
            ema26[i] = calculateEMA(closes, 26, i, (i > 0) ? ema26[i - 1] : closes[0]);
            indicators[i].macd = ema12[i] - ema26[i];
        }
        if (closes.size() >= 9) { // Ensure enough data for signal line
            vector<double> macdValues(closes.size());
            for (size_t i = 0; i < closes.size(); i++) {
                macdValues[i] = indicators[i].macd;
            }
            vector<double> macdSignal(closes.size(), 0.0);
            for (size_t i = 0; i < closes.size(); i++) {
                vector<double> macdSlice(macdValues.begin(), macdValues.begin() + i + 1);
                macdSignal[i] = calculateEMA(macdSlice, 9, i, (i > 0) ? macdSignal[i - 1] : macdValues[0]);
                indicators[i].macd_signal = macdSignal[i];
                indicators[i].macd_hist = indicators[i].macd - indicators[i].macd_signal;
            }
        }

        // EMA (20 period)
        for (size_t i = 0; i < closes.size(); i++) {
            indicators[i].ema = calculateEMA(closes, 20, i, (i > 0) ? indicators[i - 1].ema : closes[0]);
        }

        // Bollinger Bands (20 period, 2 std)
        if (closes.size() >= 20) {
            for (size_t i = 19; i < closes.size(); i++) {
                double sum = 0.0, sumSq = 0.0;
                for (int j = i - 19; j <= i; j++) sum += closes[j];
                indicators[i].bb_middle = sum / 20.0;
                for (int j = i - 19; j <= i; j++) {
                    double diff = closes[j] - indicators[i].bb_middle;
                    sumSq += diff * diff;
                }
                double stdDev = sqrt(sumSq / 20.0);
                indicators[i].bb_upper = indicators[i].bb_middle + 2 * stdDev;
                indicators[i].bb_lower = indicators[i].bb_middle - 2 * stdDev;
            }
        }

        // ATR (14 period)
        if (closes.size() >= 14) {
            vector<double> tr(closes.size(), 0.0);
            for (size_t i = 1; i < closes.size(); i++) {
                double hl = highs[i] - lows[i];
                double hpc = fabs(highs[i] - closes[i - 1]);
                double lpc = fabs(lows[i] - closes[i - 1]);
                tr[i] = hl;
                if (hpc > tr[i]) tr[i] = hpc;
                if (lpc > tr[i]) tr[i] = lpc;
            }
            double sumTR = 0.0;
            for (int i = 1; i <= 14; i++) sumTR += tr[i];
            indicators[14].atr = sumTR / 14.0;
            for (size_t i = 15; i < closes.size(); i++) {
                indicators[i].atr = (indicators[i - 1].atr * 13 + tr[i]) / 14.0;
            }
        }
    }

public:
    // **Constructor**
    CryptoTradingBot(const string& selectedMarket, bool simulationMode)
        : market(selectedMarket), entryPrice(0.0), isSimulation(simulationMode) {
        size_t pos = market.find("-");
        if (pos != string::npos) {
            cryptoAsset = market.substr(0, pos);
            fiatAsset = market.substr(pos + 1);
        }
        if (isSimulation) {
            simFiatBalance = 1000.0;
            simCryptoBalance = 0.0;
            tradeLogFile = "sim_trades.log";
            profitLogFile = "sim_log.txt";
        }
        else {
            tradeLogFile = "trades.log";
            profitLogFile = "log.txt";
        }
        totalProfitLoss = loadTotalProfitLoss();
        for (const auto& interval : intervals) {
            lastSavedTimestamps[interval] = 0;
        }
    }

    // **Fetch candles for all intervals**
    void fetchAllCandles(int limit = 100) {
        for (const auto& interval : intervals) {
            fetchCandles(interval, limit);
            calculateIndicators(interval);
        }
    }

    // **Fetch candles for a specific interval**
    bool fetchCandles(const string& interval, int limit = 100) {
        string endpoint = market + "/candles?interval=" + interval + "&limit=" + to_string(limit);
        json response = apiRequest(endpoint);
        if (response.is_array() && !response.empty()) {
            vector<vector<string>> newCandles;
            for (const auto& candle : response) {
                if (candle.is_array() && candle.size() >= 6) {
                    vector<string> candleData;
                    for (const auto& value : candle) {
                        candleData.push_back(value.is_string() ? value.get<string>() : to_string(value.get<double>()));
                    }
                    newCandles.push_back(candleData); // [timestamp, open, high, low, close, volume]
                }
            }
            if (!newCandles.empty()) {
                auto& existingCandles = candlesByInterval[interval];
                if (!existingCandles.empty()) {
                    long long lastKnownTimestamp = stoll(existingCandles.back()[0]);
                    auto it = find_if(newCandles.begin(), newCandles.end(),
                        [lastKnownTimestamp](const vector<string>& candle) {
                            return stoll(candle[0]) > lastKnownTimestamp;
                        });
                    if (it != newCandles.end()) {
                        existingCandles.insert(existingCandles.end(), it, newCandles.end());
                        lastTimestamps[interval] = stoll(existingCandles.back()[0]);
                    }
                }
                else {
                    existingCandles = newCandles;
                    lastTimestamps[interval] = stoll(existingCandles.back()[0]);
                }
                cout << "Fetched " << newCandles.size() << " candles for " << market << " (" << interval << "), "
                    << "stored " << existingCandles.size() << " total" << endl;
                return true;
            }
            cerr << "No valid candles in response for interval " << interval << endl;
            return false;
        }
        cerr << "Failed to fetch candles or invalid response format for interval " << interval << endl;
        return false;
    }

    // **Save candles to CSV file**
    void saveCandlesToCSV(const string& interval) {
        auto it = candlesByInterval.find(interval);
        if (it != candlesByInterval.end() && !it->second.empty()) {
            string filename = market + "_" + interval + "_candles.csv";
            ofstream file(filename, ios::app);
            if (file.is_open()) {
                long long lastSaved = lastSavedTimestamps[interval];
                for (const auto& candle : it->second) {
                    long long timestamp = stoll(candle[0]);
                    if (timestamp > lastSaved) {
                        file << candle[0] << "," << candle[1] << "," << candle[2] << ","
                            << candle[3] << "," << candle[4] << "," << candle[5] << "\n";
                        lastSaved = timestamp;
                    }
                }
                lastSavedTimestamps[interval] = lastSaved;
                file.close();
                cout << "Appended new candles for " << interval << " to " << filename << endl;
            }
            else {
                cerr << "Failed to open " << filename << " for writing." << endl;
            }
        }
    }

    // **Display candle data with indicators**
    void displayCandleData(const string& interval, int count = 5) {
        auto candleIt = candlesByInterval.find(interval);
        auto indIt = indicatorsByInterval.find(interval);
        if (candleIt != candlesByInterval.end() && !candleIt->second.empty() && indIt != indicatorsByInterval.end()) {
            const auto& candles = candleIt->second;
            const auto& indicators = indIt->second;
            int numToShow = count;
            if (numToShow > static_cast<int>(candles.size())) numToShow = candles.size();
            cout << "\n--- Last " << numToShow << " Candles for " << market << " (" << interval << ") ---" << endl;
            cout << "Timestamp\t\tClose\t\tRSI\t\tMACD\t\tEMA\t\tBB Upper\tATR" << endl;
            for (int i = candles.size() - numToShow; i < candles.size(); i++) {
                const auto& candle = candles[i];
                const auto& ind = indicators[i];
                time_t timestamp = stoll(candle[0]) / 1000;
                tm timeInfo;
                char buffer[25];
                gmtime_s(&timeInfo, &timestamp); // Fixed typo from previous versions
                strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeInfo);
                cout << fixed << setprecision(2);
                cout << buffer << "\t"
                    << candle[4] << "\t\t"
                    << ind.rsi << "\t\t"
                    << ind.macd << "\t\t"
                    << ind.ema << "\t\t"
                    << ind.bb_upper << "\t\t"
                    << ind.atr << endl;
            }
        }
        else {
            cout << "No candle data available for " << interval << "." << endl;
        }

    }

    // **Get current ticker price**
    double getTickerPrice() {
        json response = apiRequest("ticker/price?market=" + market);
        if (!response.empty() && response.contains("price")) {
            return stod(response["price"].get<string>());
        }
        return 0.0;
    }

    // **Get fiat balance**
    double getFiatBalance() {
        if (isSimulation) {
            return simFiatBalance;
        }
        json response = apiRequest("balance");
        if (response.is_array()) {
            for (auto& bal : response) {
                if (bal.contains("symbol") && bal["symbol"].get<string>() == fiatAsset) {
                    if (bal.contains("available"))
                        return stod(bal["available"].get<string>());
                }
            }
        }
        return 0.0;
    }

    // **Get crypto balance**
    double getCryptoBalance() {
        if (isSimulation) {
            return simCryptoBalance;
        }
        json response = apiRequest("balance");
        if (response.is_array()) {
            for (auto& bal : response) {
                if (bal.contains("symbol") && bal["symbol"].get<string>() == cryptoAsset) {
                    if (bal.contains("available"))
                        return stod(bal["available"].get<string>());
                }
            }
        }
        return 0.0;
    }

    // **Set risk parameters**
    void setRiskParameters(double maxPos) {
        maxPositionSize = maxPos;
        cout << "Risk parameters set, Max Position: " << maxPos * 100 << "%" << endl;
    }

    // **Place market order**
    bool placeMarketOrder(const string& side, double amount) {
        if (isSimulation) {
            double tickerPrice = getTickerPrice();
            if (tickerPrice == 0.0) {
                cerr << "Failed to fetch ticker price for simulation." << endl;
                return false;
            }
            if (side == "buy") {
                if (simFiatBalance >= amount) {
                    double cryptoBought = amount / tickerPrice;
                    simFiatBalance -= amount;
                    simCryptoBalance += cryptoBought;
                    entryPrice = tickerPrice;
                    boughtCryptoAmount = cryptoBought;
                    logTrade("BUY", cryptoBought, tickerPrice);
                    return true;
                }
                else {
                    cout << "Insufficient simulated fiat balance for buy order." << endl;
                    return false;
                }
            }
            else if (side == "sell") {
                if (simCryptoBalance >= amount) {
                    double fiatReceived = amount * tickerPrice;
                    simCryptoBalance -= amount;
                    simFiatBalance += fiatReceived;
                    double profitLoss = (tickerPrice - entryPrice) * amount;
                    totalProfitLoss += profitLoss;
                    logTrade("SELL", amount, tickerPrice, profitLoss);
                    saveTotalProfitLoss(totalProfitLoss);
                    entryPrice = 0.0;
                    boughtCryptoAmount = 0.0;
                    return true;
                }
                else {
                    cout << "Insufficient simulated crypto balance for sell order." << endl;
                    return false;
                }
            }
            return false;
        }
        else {
            json order;
            order["market"] = market;
            order["side"] = side;
            order["orderType"] = "market";
            if (side == "buy") {
                order["amountQuote"] = to_string(amount);
            }
            else if (side == "sell") {
                order["amount"] = to_string(amount);
            }
            string body = order.dump();
            json response = apiRequest("order", "POST", body);
            if (!response.empty()) {
                cout << "Order placed: " << response.dump() << endl;
                return true;
            }
            return false;
        }
    }

    // **Enhanced trading logic with indicators**
    void enhancedTradeLogic() {
        while (true) {
            cout << "*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#" << endl;
            fetchAllCandles(50);
            double tickerPrice = getTickerPrice();
            if (tickerPrice == 0.0) {
                cout << "Failed to fetch ticker price. Retrying in 5 seconds..." << endl;
                this_thread::sleep_for(chrono::seconds(5));
                continue;
            }
            double fiatBalance = getFiatBalance();
            double cryptoBalance = getCryptoBalance();
            cout << "Current Market: " << market
                << " | Ticker Price: " << tickerPrice
                << " | Fiat Balance (" << fiatAsset << "): " << fiatBalance
                << " | Crypto Balance (" << cryptoAsset << "): " << cryptoBalance << endl;
            displayCandleData("1h", 3);
            displayPotentialProfit(tickerPrice, cryptoBalance);

            // Example trading logic using indicators
            // Make sure you have fetched candles for "1h", "15m", and "5m" intervals before this loop

// Retrieve the indicator data from the three intervals
            auto& ind1h = indicatorsByInterval["1h"];
            auto& ind15m = indicatorsByInterval["15m"];
            auto& ind5m = indicatorsByInterval["5m"];

            if (!ind1h.empty() && !ind15m.empty() && !ind5m.empty()) {
                IndicatorData last1h = ind1h.back();
                IndicatorData last15m = ind15m.back();
                IndicatorData last5m = ind5m.back();

                // Conditions for a buy signal on each timeframe:
                bool buySignal1h = (tickerPrice < last1h.bb_lower && last1h.rsi < 30 && last1h.macd_hist > 0);
                bool buySignal15m = (tickerPrice < last15m.bb_lower && last15m.rsi < 30 && last15m.macd_hist > 0);
                bool buySignal5m = (tickerPrice < last5m.bb_lower && last5m.rsi < 30 && last5m.macd_hist > 0);

                // Buy signal is true if all three timeframes agree
                bool buySignal = buySignal1h && buySignal15m && buySignal5m;

                // Conditions for a sell signal on each timeframe:
                bool sellSignal1h = (tickerPrice > last1h.bb_upper && last1h.rsi > 70 && last1h.macd_hist < 0);
                bool sellSignal15m = (tickerPrice > last15m.bb_upper && last15m.rsi > 70 && last15m.macd_hist < 0);
                bool sellSignal5m = (tickerPrice > last5m.bb_upper && last5m.rsi > 70 && last5m.macd_hist < 0);

                // Sell signal is true if all three timeframes agree
                bool sellSignal = sellSignal1h && sellSignal15m && sellSignal5m;

                // For debugging, display a snapshot of indicator values from each timeframe
                cout << "1h -> RSI:" << last1h.rsi << " MACD Hist:" << last1h.macd_hist
                    << " BB Lower:" << last1h.bb_lower << " BB Upper:" << last1h.bb_upper << endl;
                cout << "15m -> RSI:" << last15m.rsi << " MACD Hist:" << last15m.macd_hist
                    << " BB Lower:" << last15m.bb_lower << " BB Upper:" << last15m.bb_upper << endl;
                cout << "5m -> RSI:" << last5m.rsi << " MACD Hist:" << last5m.macd_hist
                    << " BB Lower:" << last5m.bb_lower << " BB Upper:" << last5m.bb_upper << endl;

                // Execute orders based on the multi-timeframe signals
                if (cryptoBalance < 1e-8 && fiatBalance > 50 && buySignal) {
                    double positionSize = maxPositionSize * fiatBalance;
                    cout << "Buy signal detected on all timeframes!" << endl;
                    if (placeMarketOrder("buy", positionSize)) {
                        if (!isSimulation) {
                            entryPrice = tickerPrice;
                            boughtCryptoAmount = positionSize / tickerPrice;
                            logTrade("BUY", boughtCryptoAmount, tickerPrice);
                        }
                    }
                }
                else if (cryptoBalance > 0.00001 && entryPrice > 0 && sellSignal) {
                    cout << "Sell signal detected on all timeframes!" << endl;
                    if (placeMarketOrder("sell", cryptoBalance)) {
                        if (!isSimulation) {
                            double profitLoss = (tickerPrice - entryPrice) * boughtCryptoAmount;
                            totalProfitLoss += profitLoss;
                            logTrade("SELL", cryptoBalance, tickerPrice, profitLoss);
                            saveTotalProfitLoss(totalProfitLoss);
                            entryPrice = 0.0;
                            boughtCryptoAmount = 0.0;
                        }
                    }
                }
            }


            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::minutes>(now - lastSaveTime).count() >= saveIntervalMinutes) {
                for (const auto& interval : intervals) {
                    saveCandlesToCSV(interval);
                }
                lastSaveTime = now;
            }
            if (g_rateLimitRemaining != -1 && g_rateLimitResetAt != -1) {
                time_t resetAt = static_cast<time_t>(g_rateLimitResetAt / 1000);
                tm resetTime;
                char resetAtStr[30] = "Invalid timestamp";
                if (gmtime_s(&resetTime, &resetAt) == 0) {
                    strftime(resetAtStr, sizeof(resetAtStr), "%Y-%m-%d %H:%M:%S UTC", &resetTime);
                }
                cout << "Rate Limit Remaining: " << g_rateLimitRemaining
                    << " | Reset At: " << resetAtStr << endl;
            }
            cout << "Total Profit/Loss: " << totalProfitLoss << " " << fiatAsset << endl;
            if (isSimulation) {
                double initialBalance = 1000.0;
                double currentTotal = simFiatBalance + (simCryptoBalance * tickerPrice);
                double performancePercent = ((currentTotal / initialBalance) - 1.0) * 100;
                cout << "Simulation Performance: " << performancePercent << "% | "
                    << "Current Total Value: " << currentTotal << " " << fiatAsset << endl;
            }
            time_t nowTime = time(nullptr);
            tm timeInfo;
            char timeBuffer[80];
            localtime_s(&timeInfo, &nowTime);
            strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", &timeInfo);
            cout << "Last update: " << timeBuffer << " | Next update in 10 seconds..." << endl;
            this_thread::sleep_for(chrono::seconds(10));
        }
    }

    // **Display potential profit**
    void displayPotentialProfit(double tickerPrice, double cryptoBalance) {
        if (cryptoBalance > 1e-8 && entryPrice > 0) {
            double potentialProfitEuro = (tickerPrice - entryPrice) * cryptoBalance;
            double potentialProfitPercent = ((tickerPrice - entryPrice) / entryPrice) * 100;
            cout << fixed << setprecision(2);
            cout << "Potential Profit/Loss if sold now: " << potentialProfitEuro << " " << fiatAsset
                << " (" << potentialProfitPercent << "%)" << endl;
        }
        else {
            cout << "No open position." << endl;
        }
    }
};

// **Main function**
int main() {
    if (API_KEY.empty() || API_SECRET.empty()) {
        cerr << "Error: Environment variables (.ENV) BITVAVO_API_KEY and/or BITVAVO_API_SECRET are not set." << endl;
        return EXIT_FAILURE;
    }
    srand(static_cast<unsigned int>(time(0)));
    json timeResponse = apiRequest("time");
    if (!timeResponse.empty() && timeResponse.contains("time")) {
        long long serverTime = timeResponse["time"].get<long long>();
        long long localTime = chrono::duration_cast<chrono::milliseconds>(
            chrono::system_clock::now().time_since_epoch()).count();
        long long timeDiff = llabs(serverTime - localTime);
        if (timeDiff > 2000) {
            cout << "Warning: Local time is out of sync with server time by "
                << timeDiff << " milliseconds." << endl;
        }
        else {
            cout << "Time is properly synchronized with the server." << endl;
        }
    }
    else {
        cout << "Failed to fetch server time. Please check your API connection." << endl;
    }
        
    char simChoice;
    cout << "Run in simulation mode? (y/n): ";
    cin >> simChoice;
    bool simulationMode = (tolower(simChoice) == 'y');
    string selectedMarket;
    cout << "Enter market to trade (e.g., BTC-EUR): ";
    cin >> selectedMarket;
    double maxPosition;
    cout << "Enter maximum position size as percentage of balance (e.g., 25 for 25%): ";
    cin >> maxPosition;
    CryptoTradingBot bot(selectedMarket, simulationMode);
    bot.setRiskParameters(maxPosition / 100.0);
    bot.enhancedTradeLogic();
    return 0;
}