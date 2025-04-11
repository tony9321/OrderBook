#pragma once
#include <iostream>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <memory>
#include <map>
#include <list>
#include <string>
#include <thread>
#include <atomic>
#include <algorithm>
#include <limits>
#include <unordered_map>
#include <functional>
using namespace std;

enum class OrderType { Market, Limit, Stop, IOC };

struct Order {
    OrderType orderType;
    int orderId;
    double price;
    int quantity;
    string side; // "buy" or "sell"
    double stopPrice = 0.0; // For stop orders, if needed
};

using OrderPointer = shared_ptr<Order>;
using OrderList = list<OrderPointer>;

// Use TBB's concurrent_hash_map with an explicit hash compare type.
#include <tbb/concurrent_hash_map.h>
using ActiveOrdersMap = tbb::concurrent_hash_map<int, OrderPointer, tbb::tbb_hash_compare<int>>;

// use the tbb concurrent queue
#include <tbb/concurrent_queue.h>

class OrderBook {
private:
    // Buy orders ordered by price descending; Sell orders ordered by price ascending.
    map<double, OrderList, greater<double>> buyOrders_;
    map<double, OrderList> sellOrders_;
    
    // Use TBB's concurrent_hash_map for order tracking.
    ActiveOrdersMap activeOrders_;

    // Mutex for protecting order book operations.
    mutex mtx_;
    atomic<bool> running_{true};
    
    // Queues for asynchronous processing.
    tbb::concurrent_queue<OrderPointer> buyQueue_;
    tbb::concurrent_queue<OrderPointer> sellQueue_;
    
    // Matching function for buy orders.
    void matchBuyOrder(OrderPointer buyOrder) {
        lock_guard<mutex> lock(mtx_);
        // For market orders, we ignore price checks.
        while(buyOrder->quantity > 0 && !sellOrders_.empty() &&
             (buyOrder->orderType == OrderType::Market || sellOrders_.begin()->first <= buyOrder->price)) {
            double bestAsk = sellOrders_.begin()->first;
            auto &sellList = sellOrders_.begin()->second;
            OrderPointer sellOrder = sellList.front();
            // If the sell order is already zero, remove it.
            if(sellOrder->quantity <= 0) {
                sellList.pop_front();
                if(sellList.empty())
                    sellOrders_.erase(bestAsk);
                continue;
            }
            int tradeQty = min(buyOrder->quantity, sellOrder->quantity);
            if(tradeQty <= 0)
                break;
            cout << "Trade executed: Buy order " << buyOrder->orderId
                 << " and Sell order " << sellOrder->orderId
                 << " for quantity " << tradeQty
                 << " at price " << bestAsk << "\n";
            buyOrder->quantity -= tradeQty;
            sellOrder->quantity -= tradeQty;
            if(sellOrder->quantity == 0) {
                // Erase from active orders using key
                activeOrders_.erase(sellOrder->orderId);
                sellList.pop_front();
                if(sellList.empty())
                    sellOrders_.erase(bestAsk);
            }
            if(buyOrder->quantity == 0)
            {
                activeOrders_.erase(buyOrder->orderId);
                double buyPrice=buyOrder->price;
                auto it=buyOrders_.find(buyPrice);
                if(it!=buyOrders_.end())
                {
                    auto &buyList=it->second;
                    buyList.remove_if([&](const OrderPointer &o){return o->orderId==buyOrder->orderId;});
                    if(buyList.empty()) buyOrders_.erase(it);
                }
            }
        }
    }
    
    // Matching function for sell orders.
    void matchSellOrder(OrderPointer sellOrder) {
        lock_guard<mutex> lock(mtx_);
        while(sellOrder->quantity > 0 && !buyOrders_.empty() &&
             (sellOrder->orderType == OrderType::Market || buyOrders_.begin()->first >= sellOrder->price)) {
            double bestBid = buyOrders_.begin()->first;
            auto &buyList = buyOrders_.begin()->second;
            OrderPointer buyOrder = buyList.front();
            if(sellOrder->quantity <= 0) {
                buyList.pop_front();
                if(buyList.empty()) sellOrders_.erase(bestBid);
                continue;
            }
            int tradeQty = min(sellOrder->quantity, buyOrder->quantity);
            if(tradeQty <= 0)   break;
            cout << "Trade executed: Sell order " << sellOrder->orderId
                 << " and Buy order " << buyOrder->orderId
                 << " for quantity " << tradeQty
                 << " at price " << bestBid << "\n";
            sellOrder->quantity -= tradeQty;
            buyOrder->quantity -= tradeQty;
            if(buyOrder->quantity == 0) {
                activeOrders_.erase(buyOrder->orderId);
                buyList.pop_front();
                if(buyList.empty()) buyOrders_.erase(bestBid);
            }
            if(sellOrder->quantity == 0)
            {
                activeOrders_.erase(sellOrder->orderId);
                double sellPrice=sellOrder->price;
                auto it=sellOrders_.find(sellPrice);
                if(it!=sellOrders_.end())
                {
                    auto &sellList=it->second;
                    sellList.remove_if([&](const OrderPointer &o){return o->orderId==sellOrder->orderId;});
                    if(sellList.empty()) sellOrders_.erase(it);
                }
            }
        }
    }
    
public:
    // Clears the order book.
    inline void reset() {
        lock_guard<mutex> lock(mtx_);
        buyOrders_.clear();
        sellOrders_.clear();
        activeOrders_.clear();
        cout << "[OrderBook] reset\n";
    }
    
    // Add an order to the book.
    inline void addOrder(int orderId, double price, int quantity, 
                         const string& side, OrderType orderType)
    {
        OrderPointer order = make_shared<Order>(Order{orderType, orderId, price, quantity, side});
        if(orderType == OrderType::Market) {
            // Process market orders immediately.
            processOrder(order);
            cout << "[OrderBook] Market order processed immediately -> ID=" << orderId
                 << ", side=" << side << "\n";
            return;
        }
        if(orderType == OrderType::IOC) {
            // Process IOC orders immediately.
            processOrder(order);
            if(order->quantity > 0) {
                cout << "[OrderBook] IOC order partially filled -> ID=" << orderId
                     << ", side=" << side << ", remaining quantity: " << order->quantity << "\n";
                order->quantity = 0;
            }
            return;
        }
        {
            lock_guard<mutex> lock(mtx_);
            // Use TBB's accessor API instead of operator[].
            ActiveOrdersMap::accessor acc;
            activeOrders_.insert(acc, orderId);
            acc->second = order;
            
            if(side == "buy") {
                buyOrders_[price].push_back(order);
                buyQueue_.push(order);
            } else {
                sellOrders_[price].push_back(order);
                sellQueue_.push(order);
            }
        }
        cout << "[OrderBook] addOrder -> ID=" << orderId << ", side=" << side << "\n";
    }
    
    // Display the current order book.
    inline void displayOrders() {
        lock_guard<mutex> lock(mtx_);
        cout << "[OrderBook] displayOrders\n";
        cout << "Buy Orders:\n";
        for(auto &kv : buyOrders_) {
            cout << "  Price " << kv.first << ": ";
            for(auto &ord : kv.second)
                cout << "(ID=" << ord->orderId << ", qty=" << ord->quantity << ") ";
            cout << "\n";
        }
        cout << "Sell Orders:\n";
        for(auto &kv : sellOrders_) {
            cout << "  Price " << kv.first << ": ";
            for(auto &ord : kv.second)
                cout << "(ID=" << ord->orderId << ", qty=" << ord->quantity << ") ";
            cout << "\n";
        }
    }
    
    // Return the current best bid.
    inline double getBestBid() {
        lock_guard<mutex> lock(mtx_);
        if(!buyOrders_.empty())
            return buyOrders_.begin()->first;
        return 0.0;
    }
    
    // Return the current best ask.
    inline double getBestAsk() {
        lock_guard<mutex> lock(mtx_);
        if(!sellOrders_.empty())
            return sellOrders_.begin()->first;
        return 0.0;
    }
    
    // Cancel an order by its ID.
    inline bool cancelOrder(int orderId) {
        lock_guard<mutex> lock(mtx_);
        ActiveOrdersMap::accessor acc;
        if(!activeOrders_.find(acc, orderId))
            return false; // not found
    
        OrderPointer order = acc->second;
        double price = order->price;
        if(order->side == "buy") {
            auto mapIt = buyOrders_.find(price);
            if(mapIt != buyOrders_.end()) {
                auto &lst = mapIt->second;
                lst.remove_if([orderId](const OrderPointer &o){ return o->orderId == orderId; });
                if(lst.empty())
                    buyOrders_.erase(mapIt);
            }
        } else {
            auto mapIt = sellOrders_.find(price);
            if(mapIt != sellOrders_.end()) {
                auto &lst = mapIt->second;
                lst.remove_if([orderId](const OrderPointer &o){ return o->orderId == orderId; });
                if(lst.empty())
                    sellOrders_.erase(mapIt);
            }
        }
        activeOrders_.erase(acc);
        cout << "[OrderBook] cancelOrder -> ID=" << orderId << "\n";
        return true;
    }
    
    // Modify an order's quantity and price.
    inline bool modifyOrder(int orderId, int newQuantity, double newPrice) {
        lock_guard<mutex> lock(mtx_);
        ActiveOrdersMap::accessor acc;
        if(!activeOrders_.find(acc, orderId))
            return false;
        OrderPointer order = acc->second;
        double oldPrice = order->price;
        if(order->side == "buy") {
            auto mapIt = buyOrders_.find(oldPrice);
            if(mapIt != buyOrders_.end()) {
                auto &lst = mapIt->second;
                lst.remove_if([orderId](const OrderPointer &o){ return o->orderId == orderId; });
                if(lst.empty())
                    buyOrders_.erase(mapIt);
            }
        } else {
            auto mapIt = sellOrders_.find(oldPrice);
            if(mapIt != sellOrders_.end()) {
                auto &lst = mapIt->second;
                lst.remove_if([orderId](const OrderPointer &o){ return o->orderId == orderId; });
                if(lst.empty())
                    sellOrders_.erase(mapIt);
            }
        }
    
        order->price = newPrice;
        order->quantity = newQuantity;
        if(order->side == "buy") {
            buyOrders_[newPrice].push_back(order);
            buyQueue_.push(order);
        } else {
            sellOrders_[newPrice].push_back(order);
            sellQueue_.push(order);
        }
        cout << "[OrderBook] modifyOrder -> ID=" << orderId << "\n";
        return true;
    }
    
    // Asynchronous processing thread for buy orders.
    inline void processBuyOrders() {
        OrderPointer order;
        while(running_)
        {
            //try_pop is non-blocking, sleep if the queue is empty
            if(buyQueue_.try_pop(order)) matchBuyOrder(order);
            else this_thread::sleep_for(chrono::milliseconds(1));
        }
    }
    
    // Asynchronous processing thread for sell orders.
    inline void processSellOrders() {
        OrderPointer order;
        while (running_) {
            if (sellQueue_.try_pop(order))
                matchSellOrder(order);
            else
                this_thread::sleep_for(chrono::milliseconds(1));
        }
    }
    
    // Synchronous processing method (for immediate processing).
    inline void processOrder(OrderPointer order) {
        if(order->side == "buy")
            matchBuyOrder(order);
        else if(order->side == "sell")
            matchSellOrder(order);
    }
    
    // Stop the processing threads.
    inline void stopProcessing() {
        running_ = false;
    }
};