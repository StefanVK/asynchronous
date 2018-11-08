
// Boost.Asynchronous library
//  Copyright (C) Christophe Henry 2013
//
//  Use, modification and distribution is subject to the Boost
//  Software License, Version 1.0.  (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// For more information, see http://www.boost.org

#include <vector>
#include <set>
#include <future>

#include <boost/asynchronous/scheduler/single_thread_scheduler.hpp>
#include <boost/asynchronous/queue/lockfree_queue.hpp>
#include <boost/asynchronous/scheduler_shared_proxy.hpp>
#include <boost/asynchronous/scheduler/io_threadpool_scheduler.hpp>

#include <boost/asynchronous/servant_proxy.hpp>
#include <boost/asynchronous/post.hpp>
#include <boost/asynchronous/trackable_servant.hpp>

#include <boost/test/unit_test.hpp>
#include <boost/thread/future.hpp>

namespace
{
// main thread id
boost::thread::id main_thread_id;
bool dtor_called=false;

struct Servant : boost::asynchronous::trackable_servant<>
{
    typedef int simple_ctor;
    Servant(boost::asynchronous::any_weak_scheduler<> scheduler)
        : boost::asynchronous::trackable_servant<>(scheduler,
                                               boost::asynchronous::make_shared_scheduler_proxy<
                                                   boost::asynchronous::io_threadpool_scheduler<
                                                           boost::asynchronous::lockfree_queue<
                                                                BOOST_ASYNCHRONOUS_DEFAULT_JOB,
                                                                boost::asynchronous::lockfree_size>>>(2,4))
        , m_dtor_done(new std::promise<void>)
        , m_counter(0)
        , m_current(0)
    {
    }
    ~Servant()
    {
        dtor_called=true;
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant dtor not posted.");
        this->m_tracking.reset();
        if (!!m_dtor_done)
            m_dtor_done->set_value();
    }
    std::future<void> start_async_work(size_t cpt)
    {
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant start_async_work not posted.");
        m_counter = cpt;
        m_current=0;
        m_worker_ids.clear();
        std::shared_ptr<std::promise<void> > apromise(new std::promise<void>);
        std::promise<void> block_promise;
        std::shared_future<void> fub = block_promise.get_future();
        std::vector<std::shared_future<void> > first_tasks;
        // start long tasks
        for (size_t i = 0; i< m_counter ; ++i)
        {
            std::shared_ptr<std::promise<void> > bpromise(new std::promise<void>);
            if (i<4)
            {
                std::shared_future<void> fubp = bpromise->get_future();
                first_tasks.push_back(fubp);
            }
            post_callback(
               [fub,bpromise]()mutable{ bpromise->set_value();fub.get(); return boost::this_thread::get_id(); },
               [this,apromise](boost::asynchronous::expected<boost::thread::id> fu)
               {
                    ++this->m_current;
                    this->m_worker_ids.insert(fu.get());
                    if (this->m_current == this->m_counter)
                    {
                        if (this->m_counter <= 2)
                            BOOST_CHECK_MESSAGE(m_worker_ids.size()==2,"incorrect number of workers.");
                        else if (this->m_counter == 3)
                            BOOST_CHECK_MESSAGE(m_worker_ids.size()==3,"incorrect number of workers.");
                        else
                            BOOST_CHECK_MESSAGE(m_worker_ids.size()==4,"incorrect number of workers.");
                        apromise->set_value();
                    }
               }
            );
        }
        boost::wait_for_all(first_tasks.begin(),first_tasks.end());
        block_promise.set_value();
        return apromise->get_future();
    }

    std::future<void> test_many_tasks(size_t cpt)
    {
        this->set_worker(boost::asynchronous::make_shared_scheduler_proxy<
                         boost::asynchronous::io_threadpool_scheduler<
                                 boost::asynchronous::lockfree_queue<>>>(8,16));
        BOOST_CHECK_MESSAGE(main_thread_id!=boost::this_thread::get_id(),"servant test_many_tasks not posted.");
        m_current=0;
        std::shared_ptr<std::promise<void> > apromise(new std::promise<void>);
        std::promise<void> block_promise;
        auto fub = block_promise.get_future();
        // start long tasks
        for (size_t i = 0; i< cpt ; ++i)
        {
            post_callback(
               []()mutable{},
               [this,cpt,apromise](boost::asynchronous::expected<void>)
               {
                    ++this->m_current;
                    if (this->m_current == cpt)
                    {
                        apromise->set_value();
                    }
               }
            );
        }
        return apromise->get_future();
    }

std::shared_ptr<std::promise<void> > m_dtor_done;
size_t m_counter;
size_t m_current;
std::set<boost::thread::id> m_worker_ids;
};

class ServantProxy : public boost::asynchronous::servant_proxy<ServantProxy,Servant>
{
public:
    template <class Scheduler>
    ServantProxy(Scheduler s):
        boost::asynchronous::servant_proxy<ServantProxy,Servant>(s)
    {}
    BOOST_ASYNC_FUTURE_MEMBER(start_async_work)
    BOOST_ASYNC_FUTURE_MEMBER(test_many_tasks)
};

}

BOOST_AUTO_TEST_CASE( test_io_threadpool_scheduler_2 )
{
    dtor_called =false;
    main_thread_id = boost::this_thread::get_id();
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>();

       {
           ServantProxy proxy(scheduler);
           auto fuv = proxy.start_async_work(2);
           // wait for task to start
           auto fud = fuv.get();
           fud.get();
       }
    }
    // at this point, the dtor has been called
    BOOST_CHECK_MESSAGE(dtor_called,"servant dtor not called.");
}
BOOST_AUTO_TEST_CASE( test_io_threadpool_scheduler_5 )
{
    dtor_called =false;
    main_thread_id = boost::this_thread::get_id();
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>();

       {
           ServantProxy proxy(scheduler);
           auto fuv = proxy.start_async_work(5);
           // wait for task to start
           auto fud = fuv.get();
           fud.get();
       }
    }
    // at this point, the dtor has been called
    BOOST_CHECK_MESSAGE(dtor_called,"servant dtor not called.");
}
BOOST_AUTO_TEST_CASE( test_io_threadpool_scheduler_9 )
{
    dtor_called =false;
    main_thread_id = boost::this_thread::get_id();
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>();

       {
           ServantProxy proxy(scheduler);
           auto fuv = proxy.start_async_work(9);
           // wait for task to start
           auto fud = fuv.get();
           fud.get();
       }
    }
    // at this point, the dtor has been called
    BOOST_CHECK_MESSAGE(dtor_called,"servant dtor not called.");
}
BOOST_AUTO_TEST_CASE( test_io_threadpool_scheduler_5_2 )
{
    dtor_called =false;
    main_thread_id = boost::this_thread::get_id();
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>();

       {
           ServantProxy proxy(scheduler);
           auto fuv = proxy.start_async_work(5);
           // wait for task to start
           auto fud = fuv.get();
           fud.get();

           fuv = proxy.start_async_work(2);
           // wait for task to start
           fud = fuv.get();
           fud.get();
       }
    }
    // at this point, the dtor has been called
    BOOST_CHECK_MESSAGE(dtor_called,"servant dtor not called.");
}
BOOST_AUTO_TEST_CASE( test_io_threadpool_many_tasks )
{
    dtor_called =false;
    main_thread_id = boost::this_thread::get_id();
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<boost::asynchronous::single_thread_scheduler<
                                                                            boost::asynchronous::lockfree_queue<>>>();

       {
           ServantProxy proxy(scheduler);
           auto fuv = proxy.test_many_tasks(1000);
           // wait for task to start
           auto fud = fuv.get();
           fud.get();
           //std::cout << "before dtor" << std::endl;
       }
    }
    //std::cout << "after dtor" << std::endl;
    // at this point, the dtor has been called
    BOOST_CHECK_MESSAGE(dtor_called,"servant dtor not called.");
}
BOOST_AUTO_TEST_CASE( test_io_threadpool_scheduler_dtor )
{
    typedef boost::asynchronous::any_loggable servant_job;

    for (std::size_t c=0; c<2; ++c)
    {
        auto scheduler = boost::asynchronous::make_shared_scheduler_proxy<
                            boost::asynchronous::io_threadpool_scheduler<
                                boost::asynchronous::lockfree_queue<
                                servant_job,
                                boost::asynchronous::lockfree_size>>>(1,8);
        std::vector<std::future<void>> futures;
        for (std::size_t i=0; i<6; ++i)
        {
            futures.emplace_back(
            boost::asynchronous::post_future(scheduler,
                                             [](){boost::this_thread::sleep(boost::posix_time::milliseconds(1000));},
                                             ",0,0"));
        }
        boost::wait_for_all(futures.begin(), futures.end());
        //auto fu=
        boost::asynchronous::post_future(scheduler,
                                         [](){boost::this_thread::sleep(boost::posix_time::milliseconds(1000));},
                                         ",0,0");
        //fu.get();
    }
}
