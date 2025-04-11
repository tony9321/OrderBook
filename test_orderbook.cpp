// using catch2 to do unit testing for concurrency
#define CATCH_CONFIG_MAIN
#include "OrderBook.hpp"
#include "catch.hpp"
#include <thread>
#include <chrono>
#include <iostream>
#include <random>
#include <vector>
using namespace std;

//func to calc median
double computeMedian(vector<long long>& v)
{
    if(v.empty()) return 0.0;
    sort(v.begin(), v.end());
    size_t mid=v.size()/2;
    if(v.size()%2==0) return (v[mid-1]+v[mid])/2.0;
    return v[mid];
}

//func to calc 99th percentile
double computePercentile(vector<long long>& v, double percentile)
{
    if(v.empty()) return 0.0;
    sort(v.begin(), v.end());
    size_t index=static_cast<size_t>(percentile/100.0*v.size());
    if(index>=v.size()) index=v.size()-1;
    return v[index];
}

TEST_CASE("Concurrent stress test on OrderBook", "[OrderBook][stress]")
{
    auto start=chrono::high_resolution_clock::now();

    OrderBook book;
    //start async order processing
    const int processorThreads = 8;
    vector<thread> processors;
    //launch half of the processors for buy orders and half for sell orders
    for(int i=0;i<processorThreads/2;i++)
        processors.emplace_back(&OrderBook::processBuyOrders, &book);
    for(int i=0;i<processorThreads/2;i++)
        processors.emplace_back(&OrderBook::processSellOrders, &book);
    // store latencies from each thread
    vector<long long> allLatencies;
    mutex latenciesMutex;

    //lambda to add random orders concurrently, mixes Limit, Market, IOC
    auto addOrders=[&book, &latenciesMutex, &allLatencies](int startId, int orderCount)
    {
        default_random_engine generator(random_device{}());
        uniform_real_distribution<double> priceDist(90.0, 110.0);
        uniform_int_distribution<int> qtyDist(1, 100);
        uniform_int_distribution<int> orderTypeDist(0, 2);//0=Limit, 1=Market, 2=IOC
        // thread-local storage for latencies
        vector<long long> localLatencies;
        localLatencies.reserve(orderCount);
        for(int i=0;i<orderCount;i++)
        {
            int orderId=startId+i;
            double price=priceDist(generator);
            int quantity=qtyDist(generator);
            OrderType orderType;
            int typeRoll=orderTypeDist(generator);
            switch(typeRoll)
            {
                case 0:orderType=OrderType::Limit; break;
                case 1:orderType=OrderType::Market; break;
                case 2:orderType=OrderType::IOC; break;
                default:orderType=OrderType::Limit; break;
            }
            //for Limit orders use alternating sides. For market/IOC orers, choose randomly
            string side;
            if(orderType==OrderType::Limit) side=(orderId%2==0)? "buy":"sell";
            else side=(generator()%2==0)? "buy":"sell";
            //start timing addOrder
            auto startTs=chrono::high_resolution_clock::now();
            book.addOrder(orderId, price, quantity, side, orderType);
            auto endTs=chrono::high_resolution_clock::now();   
            auto diff=chrono::duration_cast<chrono::microseconds>(endTs-startTs).count();
            localLatencies.push_back(diff);
        }   
        // merge thread-local latencies into global vector
        {
            lock_guard<mutex> lk(latenciesMutex);
            allLatencies.insert(allLatencies.end(), localLatencies.begin(), localLatencies.end());
        } 
    };    
    //launch several threads to add orders concurrently
    const int adderThreadCount=8;
    const int ordersPerThread=2000;
    vector<thread> adders;

    for(int i=0;i<adderThreadCount;i++) adders.emplace_back(addOrders, i*ordersPerThread, ordersPerThread);
    //wait for all threads to finish
    for(auto &t: adders) t.join();
    //allow time for the processing threads to work thru the orders
    this_thread::sleep_for(chrono::seconds(3));

    double bestBid = book.getBestBid();
    double bestAsk = book.getBestAsk();
    
    // Either all orders matched so the book is empty…
    if(bestBid == 0.0 && bestAsk == 0.0)    SUCCEED("Order book cleared out as expected");
    else
    {
        // …or if not, then the best bid should be lower than the best ask.
        // (This might not always hold if you deliberately allow crossing prices, so adjust the invariant to your design.)
        INFO("Order book not cleared; verifying best bid < best ask");
        REQUIRE(bestBid < bestAsk);
    }
    //stop the processors and join them
    book.stopProcessing();
    for(auto &t: processors) t.join();
    //end timer
    // Calculate stats for addOrder latencies
    if(!allLatencies.empty())
    {
        // Compute average
        long long sum = 0;
        for(auto &val : allLatencies)
            sum += val;
        double avg = static_cast<double>(sum) / allLatencies.size();
        
        double med = computeMedian(allLatencies);
        double p99 = computePercentile(allLatencies, 99.0);

        cout << "[Latency] #Samples=" << allLatencies.size() 
             << ", Avg=" << avg << " us"
             << ", Median=" << med << " us"
             << ", 99%=" << p99 << " us" 
             << endl;
    }
    else cout << "[Latency] No latencies recorded.\n";
}