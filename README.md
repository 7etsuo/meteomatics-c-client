# Meteomatics C Client

A lightweight C client for interacting with the Meteomatics Weather API. This client fetches weather data including temperature, precipitation, and wind speed for specified locations.

## Prerequisites

You'll need to install the following libraries:

```bash
# For Debian/Ubuntu
sudo apt-get update
sudo apt-get install libcurl4-openssl-dev
sudo apt-get install libjansson-dev

# For Fedora
sudo dnf install libcurl-devel
sudo dnf install jansson-devel
```

## Environment Setup

Add these environment variables to your `~/.bashrc` or `~/.zshrc`:

```bash
export METEOMATICS_USERNAME="your_username"
export METEOMATICS_PASSWORD="your_password"
```

Don't forget to source your shell configuration after adding the variables:
```bash
source ~/.bashrc  # or source ~/.zshrc
```

## Building

```bash
make
```

## Running

```bash
./main
```

## Features

- Fetches weather data including:
  - Temperature (2m above ground in Celsius)
  - Precipitation (1-hour in mm)
  - Wind speed (10m above ground in m/s)
- Secure credential management via environment variables
- Robust error handling and memory management
- JSON response processing with sensitive data filtering

## Default Configuration

- Location: San Francisco (37.7749,-122.4194)
- Time: 2024-10-23T00:00:00Z
- Format: JSON

## Cleanup

```bash
make clean
```
