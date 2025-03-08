A simple crypto trading bot that uses the Bitvavo API to trade on the Bitvavo platform.
## Features

- **Bitvavo Integration**: Connects to the Bitvavo API for live trading.
- **Crypto Trading Simulation**: Built-in simulation for testing strategies without risking real money.
- **Single Trades**: The bot will only enter one trade at a time, ensuring no overlap of positions.
- **Customizable Algorithm**: The trading logic is hardcoded, but can be easily altered by the user to fit their own needs.

## Setup

1. Get your API key and secret from Bitvavo Dashboard to a secure location. [https://account.bitvavo.com/login](https://bitvavo.com/invite?a=A2DC1BCD6B)

2. **Add your API credentials** to the `.env` file with the following format:

API_KEY=123456789

API_SECRET=123456789

## Trading Logic

The trading algorithm is based on three timeframes (1 hour, 15 minutes, and 5 minutes) and uses the following, but is not limited to, these indicators:

**Bollinger Bands (bb)**
**Relative Strength Index (RSI)**
**MACD Histogram**


## Customization

The algorithm is designed to be easily customizable. You can modify the logic for buying and selling signals to suit your preferences.

**Indicators**
**Timeframes**

