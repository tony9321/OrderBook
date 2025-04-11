#pragma once
#include "OrderBook.hpp"
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_map>
using namespace std;
class StopOrderScheduler 
{
private:
    unordered_map<int, OrderPointer> pendingStopOrders_;    
    mutex mtx_;
    OrderBook &orderBook_;
    atomic<bool> running_{true};
public:
    StopOrderScheduler(OrderBook &ob) : orderBook_(ob) {}
    void addStopOrder(OrderPointer order);
    void run();
    void stop();
};
