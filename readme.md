# OrderBook

A high-performance, concurrent Order Book implemented in C++ using Intel Threading Building Blocks (TBB) for fine-grained concurrency. This project is designed for low-latency trading scenarios and includes features like limit orders, market orders, and IOC (immediate-or-cancel) orders. It also supports asynchronous matchmaking via concurrent queues.

## Table of Contents

1. [Key Features](#key-features)  
2. [Design Overview](#design-overview)  
3. [Data Structures](#data-structures)  
4. [Concurrency Model](#concurrency-model)  
5. [Usage](#usage)  
6. [Performance & Stress Testing](#performance--stress-testing)  
7. [Build Instructions](#build-instructions)  
8. [Future Improvements](#future-improvements)  
9. [License](#license)

---

## Key Features

- **Intel TBB Integration**  
  - Using TBB’s concurrent containers (`tbb::concurrent_hash_map`, `tbb::concurrent_queue`) to efficiently handle high-volume order processing.

- **Asynchronous Matching**  
  - Dedicated threads for buy and sell queues, reducing lock contention and improving throughput.

- **Multiple Order Types**  
  - Supports Limit, Market, IOC, and (stubbed) Stop orders.

- **Scalability**  
  - Modular design enables easy extension for more complex logic (partial fills, advanced order types, etc.).

- **Low Latency**  
  - Minimizes synchronization overhead and uses fine-grained locks only where necessary. ex: OrderBook] Market order processed immediately -> ID=7997, side=sell
Trade executed: Sell order 7998 and Buy order 7996 for quantity 24 at price 100.372
[OrderBook] IOC order partially filled -> ID=7999, side=buy, remaining quantity: 56
[Latency] #Samples=16000, Avg=2089.53 us, Median=10 us, 99%=9250 us

---

## Design Overview

The `OrderBook` class maintains two primary maps:
1. `buyOrders_`: A map keyed by price in descending order (highest first).  
2. `sellOrders_`: A map keyed by price in ascending order (lowest first).

Each map entry holds a list of active orders at that price level. Orders are tracked in a TBB-based active orders map (`ActiveOrdersMap`) for quick cancelation or modification.

### Workflow

1. An order arrives via `addOrder()`.  
2. If the order is Market or IOC, it’s matched immediately in `processOrder()`.  
3. Otherwise (Limit), the order is stored and enqueued in a TBB queue (`buyQueue_` or `sellQueue_`).  
4. Concurrent threads (`processBuyOrders()` and `processSellOrders()`) match queued orders with counterparties in their respective price books.  
5. Once fully filled or canceled, orders are removed from active data structures.

---

## Data Structures

- **`std::map<double, OrderList>`** for Sell Orders  
  - Sorted ascending by price, easy to retrieve lowest ask.  
- **`std::map<double, OrderList, std::greater<double>>`** for Buy Orders  
  - Sorted descending by price, easy to retrieve highest bid.  
- **`tbb::concurrent_hash_map<int, OrderPointer>`** (`ActiveOrdersMap`)  
  - Provides thread-safe access to orders by ID, allowing fast cancel/modify operations.  
- **`tbb::concurrent_queue<OrderPointer>`** for Buy & Sell Queues  
  - Enables asynchronous matching in separate threads.

---

## Concurrency Model

- **Fine-Grained Locking**  
  - A single `std::mutex mtx_` protects order book structures during matching.  
  - This minimizes race conditions while still allowing concurrency between queue pop operations.

- **TBB Concurrent Containers**  
  - `ActiveOrdersMap` (a `tbb::concurrent_hash_map`) and `tbb::concurrent_queue` avoid extra synchronization overhead.  

- **Asynchronous Matching Threads**  
  - Multiple threads independently process buy and sell queues.  
  - Reduces contention and spreads load across CPU cores.  
  - The system can be scaled by adjusting the number of processor threads for each side.

---

## Usage

1. **Instantiate and Start Processing**  
   ```cpp
   // Create an OrderBook
   OrderBook book;

   // Start processing in separate threads
   std::thread buyThread(&OrderBook::processBuyOrders, &book);
   std::thread sellThread(&OrderBook::processSellOrders, &book);

   g++ test_orderbook.cpp -std=c++17 -ltbb -lpthread -o test_orderbook

   ./test_orderbook