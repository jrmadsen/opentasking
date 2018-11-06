//
// MIT License
// Copyright (c) 2018 Jonathan R. Madsen
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// ---------------------------------------------------------------
//
//
/// \file recursive_tasking.cc
/// \brief Example showing the usage of recursive tasking
//

#include "common/utils.hh"
#include <cassert>

#if defined(PTL_USE_GPERF)
#   include <gperftools/profiler.h>
#endif

//============================================================================//

uint64_t tbb_fibonacci(const uint64_t& n, const uint64_t& cutoff)
{
    if(n < 2)
    {
        return n;
    }
    else
    {
        uint64_t x, y;
        tbb::task_group g;
        ++task_group_counter();
        if(n >= cutoff)
        {
            g.run([&] () { x = tbb_fibonacci(n-1, cutoff); });
            g.run([&] () { y = tbb_fibonacci(n-2, cutoff); });
        }
        else
        {
            //cout << "Number of recursive task-groups: " << nrecur << endl;
            g.run([&] () { x = fibonacci(n-1); });
            g.run([&] () { y = fibonacci(n-2); });
        }
        // wait for both tasks to complete
        g.wait();
        return x + y;
    }
}

//============================================================================//

uint64_t task_fibonacci(const uint64_t& n, const uint64_t& cutoff,
                        TaskManager* taskMan)
{
    if(n < 2)
    {
        return 1;
    }
    else
    {
        uint64_t x, y;
        VoidGroup_t tg;
        ++task_group_counter();
        if(n >= cutoff)
        {
            taskMan->exec(tg, [&] () { x = task_fibonacci(n-1, cutoff, taskMan); });
            taskMan->exec(tg, [&] () { y = task_fibonacci(n-2, cutoff, taskMan); });
        }
        else
        {
            taskMan->exec(tg, [&] () { x = fibonacci(n-1); });
            taskMan->exec(tg, [&] () { y = fibonacci(n-2); });
        }
        tg.wait();
        return x + y;
    }
}

//============================================================================//

void execute_iterations(uint64_t num_iter,
                        TaskGroup_t* task_group,
                        uint64_t n,
                        uint64_t& remaining)
{
    if(remaining <= 0 || !task_group)
        return;

    if(num_iter > remaining)
        num_iter = remaining;
    remaining -= num_iter;

    // add an element of randomness
    static std::atomic<uint32_t> _counter;
    uint32_t _seed = get_seed() + (++_counter * 10000);
    get_engine().seed(_seed);

    cout << cprefix << "Submitting " << num_iter
           << " tasks computing \"fibonacci(" << n << ")\" to task manager "
           << "(" << remaining << " iterations remaining)..." << std::flush;

    TaskManager* taskManager
            = TaskRunManager::GetMasterRunManager()->GetTaskManager();

    Timer t;
    t.Start();
    for(uint32_t i = 0; i < num_iter; ++i)
    {
        int offset = get_random_int();
        taskManager->exec(*task_group, fibonacci, n + offset);
    }
    t.Stop();
    cout << " " << t << endl;
}

//============================================================================//

int main(int argc, char** argv)
{
    _pause_collection;  // VTune
    //_heap_profiler_start(get_gperf_filename(argv[0], "heap").c_str());  // gperf

#if defined(PTL_USE_TIMEMORY)
    tim::manager* manager = tim::manager::instance();
#endif

    ConsumeParameters(argc, argv);

    auto hwthreads = std::thread::hardware_concurrency();
    auto default_fib = 28;
    auto default_tg = 1;
    auto default_grain = pow(32, 1);
    auto default_ntasks = pow(32, 1);
    auto default_nthreads = hwthreads;
    // cutoff fields
    auto cutoff_high = 40;
    auto cutoff_low = 10;
    auto cutoff_incr = 5;
    auto cutoff_tasks = 1;
    long cutoff_value = 44; // greater than 45 answer exceeds INT_MAX

    // default environment controls but don't overwrite
    setenv("NUM_THREADS", std::to_string(hwthreads).c_str(), 0);
    setenv("FIBONACCI",   std::to_string(default_fib).c_str(), 0);
    setenv("GRAINSIZE",   std::to_string(default_grain).c_str(), 0);
    setenv("NUM_TASKS",   std::to_string(default_ntasks).c_str(), 0);
    setenv("NUM_TASK_GROUPS", std::to_string(default_tg).c_str(), 0);

    rng_range = GetEnv<decltype(rng_range)> ("RNG_RANGE", rng_range,
                                             "Setting RNG range to +/- this value");
    unsigned numThreads = GetEnv<unsigned>  ("NUM_THREADS", default_nthreads,
                                             "Getting the number of threads");
    uint64_t nfib       = GetEnv<uint64_t>  ("FIBONACCI", default_fib,
                                             "Setting the centerpoint of fib work distribution");
    uint64_t grainsize  = GetEnv<uint64_t>  ("GRAINSIZE", numThreads,
                                             "Dividing number of task into grain of this size");
    uint64_t num_iter   = GetEnv<uint64_t>  ("NUM_TASKS", numThreads * numThreads,
                                             "Setting the number of total tasks");
    uint64_t num_groups = GetEnv<uint64_t>  ("NUM_TASK_GROUPS", 4,
                                             "Setting the number of task groups");

    cutoff_high  = GetEnv<int>("CUTOFF_HIGH", cutoff_high);
    cutoff_incr  = GetEnv<int>("CUTOFF_INCR", cutoff_incr);
    cutoff_low   = GetEnv<int>("CUTOFF_LOW",  cutoff_low);
    cutoff_tasks = GetEnv<int>("CUTOFF_TASKS", cutoff_tasks);
    cutoff_value = GetEnv<long>("CUTOFF_VALUE", cutoff_value);

    PrintEnv();

    Timer total_timer;
    total_timer.Start();

    // Construct the default run manager
    TaskRunManager* runManager = new TaskRunManager(useTBB);
    runManager->Initialize(numThreads);
    message(runManager);

    // the TaskManager is a utility that wraps the
    // function calls into tasks for the ThreadPool
    TaskManager* taskManager = runManager->GetTaskManager();

    //------------------------------------------------------------------------//
    //                                                                        //
    //                Asynchronous and Recursion examples/tests               //
    //                                                                        //
    //------------------------------------------------------------------------//
    Timer singleTimer;
    // run with async
    int64_t fib_async = 0;
    {
        singleTimer.Start();
        std::future<intmax_t> fib_tmp = taskManager->async<intmax_t>(fibonacci, cutoff_value);
        fib_async = fib_tmp.get();
        singleTimer.Stop();

        cout << prefix << "[async test] fibonacci(" << cutoff_value << ") * "
               << cutoff_tasks << " = "
               << fib_async << " ... " << singleTimer << endl;
    }

    std::vector<int> cutoffs;
    for(int i = cutoff_high; i >= cutoff_low; i -= cutoff_incr)
        cutoffs.push_back(i);

    //------------------------------------------------------------------------//
    auto run_recursive = [=] (LongGroup_t& fib_tmp, int cutoff)
    {
    #if defined(USE_TBB_TASKS)
        taskManager->exec(fib_tmp, tbb_fibonacci, cutoff_value, cutoff);
    #else
        taskManager->exec(fib_tmp, task_fibonacci, cutoff_value, cutoff, taskManager);
    #endif
    };
    //------------------------------------------------------------------------//

    std::map<int, Measurement*> measurements;
    // run with recursive
    Timer measureTimer;
    measureTimer.Start();
    for(int i = 0; i < cutoff_tasks; ++i)
    {
        cout << cprefix << "iteration #" << i << " of " << cutoff_tasks
               << "..." << endl;
        for(auto cutoff : cutoffs)
        {
            int64_t fib_recur = 0;
            task_group_counter().store(0);

            singleTimer.Start();

            if(cutoff == cutoff_high)
            {
                _resume_collection; // for VTune
            }

            LongGroup_t fib_tmp([](long& _ref, long _i) { return _ref += _i; });
            run_recursive(fib_tmp, cutoff);
            fib_recur = fib_tmp.join();

            if(cutoff == cutoff_high)
            {
                _pause_collection; // for VTune
            }

            singleTimer.Stop();

            auto num_task_groups = task_group_counter().load();

            Measurement* measurement = nullptr;
            if(measurements.find(cutoff) != measurements.end())
                measurement = measurements.find(cutoff)->second;
            if(!measurement)
            {
                measurement = new Measurement(cutoff, num_task_groups,
                                              taskManager->size());
                measurements[cutoff] = measurement;
            }

            *measurement += singleTimer;

            cout << cprefix << "[recur test] fibonacci(" << cutoff_value << ") * "
                   << cutoff_tasks << " = "
                   << fib_recur << " ... " << singleTimer
                   << " ... [# task grp] " << num_task_groups
                   << " (cutoff = " << cutoff << ") "
                   //<< measurement->real
                   << endl;

            if(fib_async != fib_recur)
                cerr << cprefix << "Warning! async != recursive: "
                     << fib_async << " != " << fib_recur << endl;
        }
    }
    measureTimer.Stop();
    std::cout << prefix << "Total measurement time: " << measureTimer << std::endl;
    std::stringstream ss;
    ss << argv[0] << "_recursive.dat";
    std::ofstream ofs(ss.str().c_str());
    if(ofs)
    {
        for(auto itr : measurements)
        {
            ofs << *(itr.second) << endl;
        }
    }
    ofs.close();

    cout << endl;

    //------------------------------------------------------------------------//
    //                                                                        //
    //                          Task-group example/test                       //
    //                                                                        //
    //------------------------------------------------------------------------//
    std::atomic_uintmax_t true_answer(0);

    // start timer for calculation
    Timer timer;
    timer.Start();

    _resume_collection; // for VTune

    ///======================================================================///
    ///                                                                      ///
    ///                                                                      ///
    ///                     PRIMARY TASKING SECTION                          ///
    ///                                                                      ///
    ///                                                                      ///
    ///======================================================================///
    // this function joins task results
    auto join = [&] (Array_t& ref, const uint64_t& thread_local_solution)
    {
        true_answer += thread_local_solution;
        //ref.push_back(thread_local_solution);
        ref.push_back(thread_local_solution);
        return ref;
    };
    //------------------------------------------------------------------------//
    // this function deletes task groups
    auto del = [] (TaskGroup_t*& _task_group)
    {
        delete _task_group;
        _task_group = nullptr;
    };
    //------------------------------------------------------------------------//
    // create a task group
    auto create = [=] (TaskGroup_t*& _task_group)
    {
        if(!_task_group)
            _task_group = new TaskGroup_t(join);
    };
    //------------------------------------------------------------------------//
    std::vector<TaskGroup_t*> task_groups(num_groups, nullptr);
    std::vector<Array_t> results(num_groups);
    uint64_t remaining = num_iter;

    while(remaining > 0)
    {
        for(uint64_t i = 0; i < task_groups.size(); ++i)
        {
            // wait for task group to finish (does join) before delete + create
            append(results[i], task_groups[i]);

            // create the task group
            create(task_groups[i]);

            // submit task with first task group
            execute_iterations(grainsize, task_groups[i], nfib, remaining);

            // wait for old task groups to finish (does join)
            if(i+1 < num_groups)
                append(results[i+1], task_groups[i+1]);

            if(remaining == 0)
                break;
        }
    }

    // make sure all task groups finished (does join)
    for(uint64_t i = 0; i < task_groups.size(); ++i)
        append(results[i], task_groups[i]);

    // compute the anser
    uint64_t answer = 0;
    for(uint64_t i = 0; i < results.size(); ++i)
    {
        answer += compute_sum(results[i]);
    }
    ///======================================================================///
    ///                                                                      ///
    ///                                                                      ///
    ///                 END OF PRIMARY TASKING SECTION                       ///
    ///                                                                      ///
    ///                                                                      ///
    ///======================================================================///

    _pause_collection; // for VTune

    // stop timer for fibonacci
    timer.Stop();

    cout << prefix << "[task group] fibonacci(" << nfib << " +/- " << rng_range
           << ") = " << answer << endl;
    cout << cprefix << "  [atomic]   fibonacci(" << nfib << " +/- " << rng_range
           << ") = " << true_answer << endl;

    std::stringstream fibprefix;
    fibprefix << "fibonacci(" << nfib << " +/- " << rng_range
              << ") calculation time: ";
    int32_t _w = static_cast<int32_t>(fibprefix.str().length()) + 2;

    cout << prefix << std::setw(_w) << fibprefix.str()
           << "\t" << timer << endl;

    // KNL hangs somewhere between finishing calculations and total_timer
    Timer del_timer;
    del_timer.Start();

    for(uint64_t i = 0; i < task_groups.size(); ++i)
        del(task_groups[i]);

    del_timer.Stop();
    cout << cprefix << std::setw(_w) << "Task group deletion time: "
           << "\t" << del_timer << endl;

    // print the time for the calculation
    total_timer.Stop();
    cout << cprefix << std::setw(_w) << "Total time: "
           << "\t" << total_timer << endl;

    int64_t ret = (true_answer - answer);
    if(ret == 0)
        cout << prefix << "Successful MT fibonacci calculation" << endl;
    else
        cout << prefix << "Failure combining MT fibonacci calculation " << endl;

    cout << endl;

    delete runManager;

    //_heap_profiler_stop;

    return ret;
}

//....oooOO0OOooo........oooOO0OOooo........oooOO0OOooo........oooOO0OOooo.....