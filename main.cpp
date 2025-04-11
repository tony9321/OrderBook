#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <limits>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <map>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>

#include "OrderBook.hpp"
#include "StopOrderScheduler.hpp"
using namespace std;

void testStopOrder(OrderBook& ob)
{
    cout << "Test Stop Order\n";
    ob.reset();
    //setup StopOrderScheduler w/ reference to order book
    StopOrderScheduler stopScheduler(ob);
    //start the scheduler in its own thread
    thread schedulerThread(&StopOrderScheduler::run, &stopScheduler);
    //add a stop order. Ex: buy stop order will be triggered when the best ask>=stopPrice, here we use the example of the stopPrice=150
    OrderPointer stopOrder=make_shared<Order>(Order{OrderType::Stop, 30, 140, 10, "buy", 150});
    stopScheduler.addStopOrder(stopOrder);

    //add an opposing sell order that raises the best ask ti 150
    ob.addOrder(31, 155, 10, "sell", OrderType::Limit);

    //allow some time for the scheduler to trigger the stop order
    this_thread::sleep_for(chrono::seconds(2));

    //stop the scheduler and join the thread
    stopScheduler.stop();
    schedulerThread.join();

    ob.displayOrders();
    cout << "Best Bid: " << ob.getBestBid() << ", Best Ask: " << ob.getBestAsk() << '\n';
}

void testIOC(OrderBook& ob) {
    cout << "Test IOC Order" << endl;
    ob.reset();
    // Ensure nonzero price if you want best bid/ask to be nonzero
    ob.addOrder(20, 100, 5, "sell", OrderType::IOC);
    this_thread::sleep_for(chrono::seconds(1));
    ob.displayOrders();
    cout << "Best Bid: " << ob.getBestBid() << ", Best Ask: " << ob.getBestAsk() << endl;
}

void testCancellation(OrderBook& ob) {
    cout << "Test Cancellation" << endl;
    ob.reset();
    ob.addOrder(10, 110, 10, "buy", OrderType::Limit);
    this_thread::sleep_for(chrono::seconds(1));
    ob.displayOrders();
    if (ob.cancelOrder(10))
        cout << "Order 10 cancelled successfully." << endl;
    else
        cout << "Failed to cancel Order 10." << endl;
    ob.displayOrders();
}

void testModification(OrderBook& ob) {
    cout << "Test Modification" << endl;
    ob.reset();
    ob.addOrder(11, 130, 10, "sell", OrderType::Limit);
    this_thread::sleep_for(chrono::seconds(1));
    ob.displayOrders();
    if (ob.modifyOrder(11, 15, 125))
        cout << "Modification successful." << endl;
    else
        cout << "Modification failed." << endl;
    ob.displayOrders();
}

void runTestScenarios(OrderBook& ob) {
    cout << "Test 1: Full match scenario" << endl;
    ob.reset();
    ob.addOrder(1, 100, 10, "buy", OrderType::Limit);
    ob.addOrder(2, 100, 10, "sell", OrderType::Limit);
    this_thread::sleep_for(chrono::seconds(1));
    ob.displayOrders();
    cout << "Best Bid: " << ob.getBestBid() << ", Best Ask: " << ob.getBestAsk() << "\n" << endl;
    
    cout << "Test 2: Partial fill scenario" << endl;
    ob.reset();
    ob.addOrder(3, 150, 20, "buy", OrderType::Limit);
    ob.addOrder(4, 150, 10, "sell", OrderType::Limit);
    this_thread::sleep_for(chrono::seconds(1));
    ob.displayOrders();
    cout << "Best Bid: " << ob.getBestBid() << ", Best Ask: " << ob.getBestAsk() << "\n" << endl;
    
    cout << "Test 3: Market order scenario" << endl;
    ob.reset();
    ob.addOrder(8, 150, 10, "buy", OrderType::Limit);
    // For Market order, best ask may be 0 if price is 0; use a nonzero price to see a value.
    ob.addOrder(5, 120, 5, "sell", OrderType::Market);
    this_thread::sleep_for(chrono::seconds(1));
    ob.displayOrders();
    cout << "Best Bid: " << ob.getBestBid() << ", Best Ask: " << ob.getBestAsk() << "\n" << endl;
    
    cout << "Test 4: No match scenario" << endl;
    ob.reset();
    ob.addOrder(6, 80, 5, "buy", OrderType::Limit);
    ob.addOrder(7, 120, 5, "sell", OrderType::Limit);
    this_thread::sleep_for(chrono::seconds(1));
    ob.displayOrders();
    cout << "Best Bid: " << ob.getBestBid() << ", Best Ask: " << ob.getBestAsk() << "\n" << endl;
}

int main() {
    OrderBook orderBook;
    
    // Start simulated processing threads
    thread buyConsumer(&OrderBook::processBuyOrders, &orderBook);
    thread sellConsumer(&OrderBook::processSellOrders, &orderBook);
    
    // Run test scenarios
    runTestScenarios(orderBook);
    testCancellation(orderBook);
    testModification(orderBook);
    testIOC(orderBook);
    testStopOrder(orderBook);
    
    // Let things settle
    this_thread::sleep_for(chrono::seconds(1));
    
    // Stop processing threads
    orderBook.stopProcessing();
    buyConsumer.join();
    sellConsumer.join();
    
    cout << "Final Order Book:" << endl;
    orderBook.displayOrders();
    cout << "Final Best Bid: " << orderBook.getBestBid()
         << ", Final Best Ask: " << orderBook.getBestAsk() << endl;
    
    return 0;
}