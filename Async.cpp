#include "Async.h"

namespace Async {

    // ASYNC ==============

    Async::Async() {

        mainThreadId = std::this_thread::get_id();
        setThreadsCount(1);
    }


    Async::~Async() {
    }

    bool Async::isMainThread() const {
        return mainThreadId == std::this_thread::get_id();
    }

    Work* Async::doSync(std::function<void()> job) {

        auto newWork = new Work(this, [job]() {
            job();
        }, 0, false, false);

        {
            std::lock_guard<std::recursive_mutex> lock(mutex);
            work.push_back(newWork);
        }

        return newWork;

    }

    Work* Async::doAsync(std::function<void()> bgJob, std::function<void()> fgNotification, bool manualWorkerStart, int threadIndex) {
        auto newWork = new Work(this, bgJob, fgNotification, manualWorkerStart, true, threadIndex);

        {
            std::lock_guard<std::recursive_mutex> lock(mutex);
            work.push_back(newWork);
        }

        return newWork;
    };


    void Async::update() {

        std::lock_guard<std::recursive_mutex> lock(mutex);

        std::vector<Work*> remove;

        for (auto it = work.begin(); it != work.end(); ++it) {
            auto& currentWork = *it;
            if (currentWork.update()) {
                onWorkDone(&currentWork);
                remove.push_back(&currentWork);
            }
        }

        for (auto work : remove) {
            removeWork(work);
        }

    }

    void Async::removeWork(Work* work) {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        auto it = std::find(this->work.begin(), this->work.end(), work);
        if ( it != this->work.end() ) {
            this->work.erase(it);
        }
    }

    void Async::setThreadsCount(unsigned value) {
        if (value == 0) {
            throw std::runtime_error("thread count can't be zero");
        }

        std::lock_guard< std::recursive_mutex > lock(mutex);

        auto size = workers.size();
        if (value == size) {
            return;
        }

        workers.reserve(value);

        if (value < size) {
            workers.erase( workers.begin() + value, workers.end() );
        } else {
            for (auto i = 0; i < value - size; ++i) {
                workers.push_back(new Worker());
            }
        }

    }

    size_t Async::getThreadsCount() const {
        std::lock_guard< std::recursive_mutex > lock(mutex);
        return workers.size();
    }

    void Async::startWork(Work *work) {
        std::lock_guard< std::recursive_mutex > lock(mutex);
        const auto& threadIndex = work->getThreadIndex();
        Worker* worker = 0;
        if (threadIndex) {
            auto indexValue = *threadIndex;
            if (indexValue > workers.size() - 1) {
                throw std::runtime_error("wrong thread index");
            }

            worker = &workers[indexValue];

        } else {
            worker = &getOptimalWorker();
        }

        worker->enqueue(work);
    }

    Worker& Async::getOptimalWorker() {
        std::lock_guard< std::recursive_mutex > lock(mutex);
        Worker* worker = &workers[0];

        for (auto it = workers.begin(); it != workers.end(); ++it) {
            auto& currentWorker = *it;
            if (currentWorker.getQueueSize() < worker->getQueueSize()) {
                worker = &currentWorker;
            }
        }

        return *worker;
    }


    // WORKER =============

    Worker::Worker()
    : stop(false)
    , thread( std::bind(&Worker::workHandler, this) )
    {

    }

    void Worker::workHandler() {
        while(!stop) {
            std::unique_lock<std::recursive_mutex> lock(workerMutex);
            cv.wait(lock, [this] {
                return !this->queue.empty() || stop.load();
            });

            tasks.swap( queue );
            queue.clear();

            lock.unlock();

            for (auto work : tasks) {
                work->run();
            }
        }

    }

    void Worker::enqueue(Work *work) {
        std::lock_guard<std::recursive_mutex> lock(workerMutex);
        queue.push_back(work);
        cv.notify_one();
    }

    Worker::~Worker() {
        stop = true;
        thread.detach();
    }

    // WORK ==============

    Work::Work(
                Async* async_,
                std::function<void()> bgJob_,
                std::function<void()> fgNotification_,
                bool manualStart_,
                bool background_,
                int threadIndex_)

            : async(async_)
            , fgNotification(fgNotification_)
            , hasDone(false)
            , job(bgJob_)
            , started(false)
            , background(background_)

    {
        if (threadIndex_ >= 0) {
            threadIndex.reset(threadIndex_);
        }

        if (!manualStart_) {
            start();
        }
    }

    void Work::start() {
        if (started)
            return;

        if (!background) {
            hasDone = true;
        } else {
            async->startWork(this);
        }

        started = true;
    }

    bool Work::update() {
        if (!hasDone) {
            return false;
        }

        if (!background) {
            job();
        }

        if (fgNotification) {
            fgNotification();
        }

        return true;
    }

    Work::~Work() {
        std::cout << "~Work\n";
    }

}
