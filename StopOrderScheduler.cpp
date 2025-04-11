#include "StopOrderScheduler.hpp"
#include <iostream>
#include <chrono>
using namespace std;

void StopOrderScheduler::addStopOrder(OrderPointer order) {
    lock_guard<mutex> lock(mtx_);
    pendingStopOrders_[order->orderId] = order;
    cout << "[StopOrderScheduler] Added stop order -> ID=" << order->orderId << "\n";
}

void StopOrderScheduler::run()
{
    while(running_)
    {
        {
            lock_guard<mutex> lock(mtx_);
            for(auto it=pendingStopOrders_.begin();it!=pendingStopOrders_.end();)
            {
                OrderPointer order = it->second;
                // check the trigger condition for stop order
                if(order->side=="buy"&&orderBook_.getBestAsk()>=order->stopPrice)
                {
                    cout << "Activating stop order" << order->orderId << "buy as Market Order\n";
                    order->orderType=OrderType::Market;
                    orderBook_.processOrder(order);
                    it=pendingStopOrders_.erase(it);
                }
                else if(order->side=="sell"&&orderBook_.getBestBid()<=order->stopPrice)
                {
                    cout << "Activating stop order" << order->orderId << "sell as Market Order\n";
                    order->orderType=OrderType::Market;
                    orderBook_.processOrder(order);
                    it=pendingStopOrders_.erase(it);
                }
                else ++it;
            }
        }
        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

void StopOrderScheduler::stop()
{
    running_ = false;
    cout << "[StopOrderScheduler] Stopping scheduler...\n";
}