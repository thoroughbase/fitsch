# Improve efficiency of Fitsch's concurrent task scheduler

### Problems

* Callbacks use `std::function`, which requires dynamic memory allocations to store bound arguments
* Function arguments (e.g. `std::string`) are copy-constructed, potentially incurring further allocations
* New `std::vector<Result>`s are allocated for each task
* New thread spawned for each task
* Common mutex lock for all tasks and results produces high contention & long waiting in heavier work loads
* No defined bounds on memory and CPU usage

### Goals

* Minimise use of and overhead from `std::function` construction
* Eliminate unnecessary copying of arguments from `Task` creation to `Result` dispatch
* Eliminate result vector reallocations
* Eliminate thread creation overhead
* Reduce data dependencies between concurrent tasks to minimise need for mutexes and deferment to OS scheduler
* Introduce bounds on resource usage

### Obstacles

* No polymorphic allocator support for `std::function`

### Solutions

* Introduce `tb::func`, a type-erasing function type which stores bound, exclusively
**trivially copyable** arguments directly inside internal members, to replace `std::function`
  - Compatible with any allocator
  - Removes one layer of indirection when accessing arguments
  - Eliminates initial allocations
  - Enforcing that arguments be trivially copyable means objects such as `std::string` must
    be allocated separately and independently of the function, and eliminates the need for
    a Deleter function object
* Change `Task` to use its task group's internal memory pool for dynamically allocated
arguments via `tb::thread_safe_memory_arena`
  - Eliminates copying, lifetime concerns and deallocation requirements
  - Tasks having a fixed size allows the use of a ring buffer for the task queue
* Introduce `tb::mpmc_queue` for storing tasks to be executed
  - Lock-free multi-producer multi-consumer queue, reducing average operation wait time
  - No allocations/deallocations required beyond program start and end
  - Requires having a fixed upper limit for task backlog
  - Well-defined upper bound on memory usage
* Change `ResultsContainer` hash map to a `std::vector<TaskGroup>`
  - No allocations/deallocations required beyond initialisation
  - Reduced cache miss rate by reusing memory regions
  - Requires memory arena for results
  - Must be at least as big as the task queue + room for external tasks
* Introduce worker thread pool
  - Eliminates thread creation overhead past initialisation

### Informal benchmarks

Average time taken to return 1000 sets of 32 allocated strings:

**Current implementation**
```
Avg task set completion time: 1630705μs
Task sets completed = 1000
Total time = 3246.487875ms
./ftest-old  0.37s user 1.40s system 54% cpu 3.253 total
```

**Proposed implementation**
```
Avg task set completion time: 401μs
Task sets completed = 1000
Total time = 323.762084ms
./ftest  0.11s user 0.07s system 54% cpu 0.334 total
```

Note: A thread sleep of 50 microseconds was inserted after each task set insertion
into the queue for the old implementation to limit CPU usage to approximately 50%.
(Total 50ms sleep)

The proposed implementation was made to wait 1 millisecond for every queue insertion
failure, with a queue size of 128. A reattempt was made every 4 insertion attempts.
(Total 250ms sleep)

Both implementations were limited to a maximum of 4 concurrent tasks.
