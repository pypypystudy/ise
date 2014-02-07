/****************************************************************************\
*                                                                            *
*  ISE (Iris Server Engine) Project                                          *
*  http://github.com/haoxingeng/ise                                          *
*                                                                            *
*  Copyright 2013 HaoXinGeng (haoxingeng@gmail.com)                          *
*  All rights reserved.                                                      *
*                                                                            *
*  Licensed under the Apache License, Version 2.0 (the "License");           *
*  you may not use this file except in compliance with the License.          *
*  You may obtain a copy of the License at                                   *
*                                                                            *
*      http://www.apache.org/licenses/LICENSE-2.0                            *
*                                                                            *
*  Unless required by applicable law or agreed to in writing, software       *
*  distributed under the License is distributed on an "AS IS" BASIS,         *
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  *
*  See the License for the specific language governing permissions and       *
*  limitations under the License.                                            *
*                                                                            *
\****************************************************************************/

///////////////////////////////////////////////////////////////////////////////
// �ļ�����: ise_server_tcp.cpp
// ��������: TCP��������ʵ��
///////////////////////////////////////////////////////////////////////////////

#include "ise/main/ise_server_tcp.h"
#include "ise/main/ise_err_msgs.h"
#include "ise/main/ise_application.h"

using namespace ise;

namespace ise
{

///////////////////////////////////////////////////////////////////////////////
// Ԥ����ְ���

void bytePacketSplitter(const char *data, int bytes, int& retrieveBytes)
{
    retrieveBytes = (bytes > 0 ? 1 : 0);
}

void linePacketSplitter(const char *data, int bytes, int& retrieveBytes)
{
    retrieveBytes = 0;

    const char *p = data;
    int i = 0;
    while (i < bytes)
    {
        if (*p == '\r' || *p == '\n')
        {
            retrieveBytes = i + 1;
            if (i < bytes - 1)
            {
                char next = *(p+1);
                if ((next == '\r' || next == '\n') && next != *p)
                    ++retrieveBytes;
            }
            break;
        }

        ++p;
        ++i;
    }
}

//-----------------------------------------------------------------------------

void nullTerminatedPacketSplitter(const char *data, int bytes, int& retrieveBytes)
{
    const char DELIMITER = '\0';

    retrieveBytes = 0;
    for (int i = 0; i < bytes; ++i)
    {
        if (data[i] == DELIMITER)
        {
            retrieveBytes = i + 1;
            break;
        }
    }
}

//-----------------------------------------------------------------------------

void anyPacketSplitter(const char *data, int bytes, int& retrieveBytes)
{
    retrieveBytes = (bytes > 0 ? bytes : 0);
}

///////////////////////////////////////////////////////////////////////////////
// class IoBuffer

IoBuffer::IoBuffer() :
    buffer_(INITIAL_SIZE),
    readerIndex_(0),
    writerIndex_(0)
{
    // nothing
}

IoBuffer::~IoBuffer()
{
    // nothing
}

//-----------------------------------------------------------------------------
// ����: �򻺴�׷��д������
//-----------------------------------------------------------------------------
void IoBuffer::append(const string& str)
{
    append(str.c_str(), (int)str.length());
}

//-----------------------------------------------------------------------------
// ����: �򻺴�׷��д������
//-----------------------------------------------------------------------------
void IoBuffer::append(const void *data, int bytes)
{
    if (data && bytes > 0)
    {
        if (getWritableBytes() < bytes)
            makeSpace(bytes);

        ISE_ASSERT(getWritableBytes() >= bytes);

        memmove(getWriterPtr(), data, bytes);
        writerIndex_ += bytes;
    }
}

//-----------------------------------------------------------------------------
// ����: �򻺴�׷�� bytes ���ֽڲ����Ϊ'\0'
//-----------------------------------------------------------------------------
void IoBuffer::append(int bytes)
{
    if (bytes > 0)
    {
        string str;
        str.resize(bytes, 0);
        append(str);
    }
}

//-----------------------------------------------------------------------------
// ����: �ӻ����ȡ bytes ���ֽ�����
//-----------------------------------------------------------------------------
void IoBuffer::retrieve(int bytes)
{
    if (bytes > 0)
    {
        ISE_ASSERT(bytes <= getReadableBytes());
        readerIndex_ += bytes;
    }
}

//-----------------------------------------------------------------------------
// ����: �ӻ����ȡȫ���ɶ����ݲ����� str ��
//-----------------------------------------------------------------------------
void IoBuffer::retrieveAll(string& str)
{
    if (getReadableBytes() > 0)
        str.assign(peek(), getReadableBytes());
    else
        str.clear();

    retrieveAll();
}

//-----------------------------------------------------------------------------
// ����: �ӻ����ȡȫ���ɶ�����
//-----------------------------------------------------------------------------
void IoBuffer::retrieveAll()
{
    readerIndex_ = 0;
    writerIndex_ = 0;
}

//-----------------------------------------------------------------------------

void IoBuffer::swap(IoBuffer& rhs)
{
    buffer_.swap(rhs.buffer_);
    std::swap(readerIndex_, rhs.readerIndex_);
    std::swap(writerIndex_, rhs.writerIndex_);
}

//-----------------------------------------------------------------------------
// ����: ��չ����ռ��Ա����д�� moreBytes ���ֽ�
//-----------------------------------------------------------------------------
void IoBuffer::makeSpace(int moreBytes)
{
    if (getWritableBytes() + getUselessBytes() < moreBytes)
    {
        buffer_.resize(writerIndex_ + moreBytes);
    }
    else
    {
        // ��ȫ���ɶ������������濪ʼ��
        int readableBytes = getReadableBytes();
        char *buffer = getBufferPtr();
        memmove(buffer, buffer + readerIndex_, readableBytes);
        readerIndex_ = 0;
        writerIndex_ = readerIndex_ + readableBytes;

        ISE_ASSERT(readableBytes == getReadableBytes());
    }
}

///////////////////////////////////////////////////////////////////////////////
// class TcpEventLoopThread

TcpEventLoopThread::TcpEventLoopThread(TcpEventLoop& eventLoop) :
    eventLoop_(eventLoop)
{
    setAutoDelete(false);
}

//-----------------------------------------------------------------------------

void TcpEventLoopThread::execute()
{
    eventLoop_.loopThreadId_ = getThreadId();
    eventLoop_.runLoop(this);
}

//-----------------------------------------------------------------------------

void TcpEventLoopThread::afterExecute()
{
    eventLoop_.loopThreadId_ = 0;
}

///////////////////////////////////////////////////////////////////////////////
// class TcpEventLoop

TcpEventLoop::TcpEventLoop() :
    thread_(NULL),
    loopThreadId_(0),
    lastCheckTimeoutTicks_(0)
{
    // nothing
}

TcpEventLoop::~TcpEventLoop()
{
    tcpConnMap_.clear();
    stop(false, true);
}

//-----------------------------------------------------------------------------
// ����: ���������߳�
//-----------------------------------------------------------------------------
void TcpEventLoop::start()
{
    if (!thread_)
    {
        thread_ = new TcpEventLoopThread(*this);
        thread_->run();
    }
}

//-----------------------------------------------------------------------------
// ����: ֹͣ�����߳�
//-----------------------------------------------------------------------------
void TcpEventLoop::stop(bool force, bool waitFor)
{
    if (thread_ && thread_->isRunning())
    {
        if (force)
        {
            thread_->kill();
            thread_ = NULL;
            waitFor = false;
        }
        else
        {
            thread_->terminate();
            wakeupLoop();
        }

        if (waitFor)
        {
            thread_->waitFor();
            delete thread_;
            thread_ = NULL;
        }
    }
}

//-----------------------------------------------------------------------------
// ����: �жϹ����̵߳�ǰ�Ƿ���������
//-----------------------------------------------------------------------------
bool TcpEventLoop::isRunning()
{
    return (thread_ != NULL && thread_->isRunning());
}

//-----------------------------------------------------------------------------
// ����: �жϵ�ǰ���ô˷������̺߳ʹ� eventLoop �����߳��ǲ���ͬһ���߳�
//-----------------------------------------------------------------------------
bool TcpEventLoop::isInLoopThread()
{
    return loopThreadId_ == getCurThreadId();
}

//-----------------------------------------------------------------------------
// ����: ȷ����ǰ�������¼�ѭ���߳���
//-----------------------------------------------------------------------------
void TcpEventLoop::assertInLoopThread()
{
    ISE_ASSERT(isInLoopThread());
}

//-----------------------------------------------------------------------------
// ����: ���¼�ѭ���߳�������ִ��ָ���ķº���
// ��ע: �̰߳�ȫ
//-----------------------------------------------------------------------------
void TcpEventLoop::executeInLoop(const Functor& functor)
{
    if (isInLoopThread())
        functor();
    else
        delegateToLoop(functor);
}

//-----------------------------------------------------------------------------
// ����: ��ָ���ķº���ί�и��¼�ѭ���߳�ִ�С��߳�����ɵ�ǰһ���¼�ѭ������
//       ִ�б�ί�еķº�����
// ��ע: �̰߳�ȫ
//-----------------------------------------------------------------------------
void TcpEventLoop::delegateToLoop(const Functor& functor)
{
    {
        AutoLocker locker(delegatedFunctors_.mutex);
        delegatedFunctors_.items.push_back(functor);
    }

    wakeupLoop();
}

//-----------------------------------------------------------------------------
// ����: ���һ�������� (finalizer) ���¼�ѭ���У���ÿ��ѭ��������ִ������
//-----------------------------------------------------------------------------
void TcpEventLoop::addFinalizer(const Functor& finalizer)
{
    AutoLocker locker(finalizers_.mutex);
    finalizers_.items.push_back(finalizer);
}

//-----------------------------------------------------------------------------
// ����: ��ָ������ע�ᵽ�� eventLoop ��
//-----------------------------------------------------------------------------
void TcpEventLoop::addConnection(TcpConnection *connection)
{
    TcpInspectInfo::instance().addConnCount.increment();

    TcpConnectionPtr connPtr(connection);
    tcpConnMap_[connection->getConnectionName()] = connPtr;

    registerConnection(connection);
    delegateToLoop(boost::bind(&IseBusiness::onTcpConnected, &iseApp().iseBusiness(), connPtr));
}

//-----------------------------------------------------------------------------
// ����: ��ָ�����ӴӴ� eventLoop ��ע��
//-----------------------------------------------------------------------------
void TcpEventLoop::removeConnection(TcpConnection *connection)
{
    TcpInspectInfo::instance().removeConnCount.increment();

    unregisterConnection(connection);

    // �˴� shared_ptr �����ݼ����п��ܻ����� TcpConnection ����
    tcpConnMap_.erase(connection->getConnectionName());
}

//-----------------------------------------------------------------------------
// ����: ���ȫ������
// ��ע:
//   conn->shutdown(true, true) ʹ IOCP ���ش��󣬴Ӷ��ͷ� IOCP ���е� shared_ptr��
//   ֮�� TcpConnection::errorOccurred() �����ã��������� onTcpDisconnected() ��
//   TcpConnection::setEventLoop(NULL)��
//-----------------------------------------------------------------------------
void TcpEventLoop::clearConnections()
{
    assertInLoopThread();

    for (TcpConnectionMap::iterator iter = tcpConnMap_.begin(); iter != tcpConnMap_.end(); ++iter)
    {
        TcpConnectionPtr conn = tcpConnMap_.begin()->second;
        conn->shutdown(true, true);
    }
}

//-----------------------------------------------------------------------------
// ����: ִ���¼�ѭ��
// ע��:
//   * ֻ��ȫ�����ӱ����ٺ�ſ����˳�ѭ������Ȼδ�������ӽ��Գ����˳��������
//     �鷳���� Linux �µ��͵Ĵ�����Ϣ��:
//     'pure virtual method called terminate called without an active exception'.
//-----------------------------------------------------------------------------
void TcpEventLoop::runLoop(Thread *thread)
{
    bool isTerminated = false;
    while (!isTerminated || !tcpConnMap_.empty())
    {
        if (thread->isTerminated())
        {
            clearConnections();
            isTerminated = true;
        }

        doLoopWork(thread);
        checkTimeout();
        executeDelegatedFunctors();
        executeFinalizer();
    }
}

//-----------------------------------------------------------------------------
// ����: ִ�б�ί�еķº���
//-----------------------------------------------------------------------------
void TcpEventLoop::executeDelegatedFunctors()
{
    Functors functors;
    {
        AutoLocker locker(delegatedFunctors_.mutex);
        functors.swap(delegatedFunctors_.items);
    }

    for (size_t i = 0; i < functors.size(); ++i)
        functors[i]();
}

//-----------------------------------------------------------------------------
// ����: ִ������������
//-----------------------------------------------------------------------------
void TcpEventLoop::executeFinalizer()
{
    Functors finalizers;
    {
        AutoLocker locker(finalizers_.mutex);
        finalizers.swap(finalizers_.items);
    }

    for (size_t i = 0; i < finalizers.size(); ++i)
        finalizers[i]();
}

//-----------------------------------------------------------------------------
// ����: ���ÿ��TCP���ӵĳ�ʱ
//-----------------------------------------------------------------------------
void TcpEventLoop::checkTimeout()
{
    const UINT CHECK_INTERVAL = 1000;  // ms

    UINT64 curTicks = getCurTicks();
    if (getTickDiff(lastCheckTimeoutTicks_, curTicks) >= (UINT64)CHECK_INTERVAL)
    {
        lastCheckTimeoutTicks_ = curTicks;

        for (TcpConnectionMap::iterator iter = tcpConnMap_.begin(); iter != tcpConnMap_.end(); ++iter)
        {
            TcpConnectionPtr& connPtr = iter->second;
            connPtr->checkTimeout(curTicks);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// class TcpEventLoopList

TcpEventLoopList::TcpEventLoopList(int loopCount) :
    items_(false, true)
{
    setCount(loopCount);
}

TcpEventLoopList::~TcpEventLoopList()
{
    // nothing
}

//-----------------------------------------------------------------------------
// ����: ����ȫ�� eventLoop �Ĺ����߳�
//-----------------------------------------------------------------------------
void TcpEventLoopList::start()
{
    for (int i = 0; i < items_.getCount(); i++)
        items_[i]->start();
}

//-----------------------------------------------------------------------------
// ����: ȫ�� eventLoop ֹͣ����
//-----------------------------------------------------------------------------
void TcpEventLoopList::stop()
{
    const int MAX_WAIT_FOR_SECS = 10;   // (��)
    const double SLEEP_INTERVAL = 0.5;  // (��)

    // ֹ֪ͨͣ
    for (int i = 0; i < items_.getCount(); i++)
        items_[i]->stop(false, false);

    // �ȴ�ֹͣ
    double waitSecs = 0;
    while (waitSecs < MAX_WAIT_FOR_SECS)
    {
        int runningCount = 0;
        for (int i = 0; i < items_.getCount(); i++)
            if (items_[i]->isRunning()) runningCount++;

        if (runningCount == 0) break;

        sleepSeconds(SLEEP_INTERVAL, true);
        waitSecs += SLEEP_INTERVAL;
    }

    // ǿ��ֹͣ
    for (int i = 0; i < items_.getCount(); i++)
        items_[i]->stop(true, true);
}

//-----------------------------------------------------------------------------
// ����: �� connection �ҽӵ� EventLoop ��
// ����:
//   index - EventLoop ����� (0-based)��Ϊ -1 ��ʾ�Զ�ѡ��
//-----------------------------------------------------------------------------
bool TcpEventLoopList::registerToEventLoop(BaseTcpConnection *connection, int eventLoopIndex)
{
    TcpEventLoop *eventLoop = NULL;

    if (eventLoopIndex >= 0 && eventLoopIndex < getCount())
    {
        AutoLocker locker(mutex_);
        eventLoop = getItem(eventLoopIndex);
    }
    else
    {
        static int s_index = 0;
        AutoLocker locker(mutex_);
        // round-robin
        eventLoop = getItem(s_index);
        s_index = (s_index >= getCount() - 1 ? 0 : s_index + 1);
    }

    bool result = (eventLoop != NULL);
    if (result)
    {
        // �� ((TcpConnection*)connection)->setEventLoop(eventLoop) ί�и��¼�ѭ���߳�
        eventLoop->delegateToLoop(boost::bind(
            &TcpConnection::setEventLoop,
            static_cast<TcpConnection*>(connection),
            eventLoop));
    }

    return result;
}

//-----------------------------------------------------------------------------
// ����: ���� eventLoop �ĸ���
//-----------------------------------------------------------------------------
void TcpEventLoopList::setCount(int count)
{
    count = ensureRange(count, 1, (int)MAX_LOOP_COUNT);

    for (int i = 0; i < count; i++)
    {
#ifdef ISE_WINDOWS
        items_.add(new WinTcpEventLoop());
#endif
#ifdef ISE_LINUX
        items_.add(new LinuxTcpEventLoop());
#endif
    }
}

///////////////////////////////////////////////////////////////////////////////
// class TcpConnection

TcpConnection::TcpConnection()
{
    init();
    TcpInspectInfo::instance().tcpConnCreateCount.increment();
}

TcpConnection::TcpConnection(TcpServer *tcpServer, SOCKET socketHandle) :
    BaseTcpConnection(socketHandle)
{
    init();

    tcpServer_ = tcpServer;
    tcpServer_->incConnCount();
    TcpInspectInfo::instance().tcpConnCreateCount.increment();
}

TcpConnection::~TcpConnection()
{
    //logger().writeFmt("destroy conn: %s", getConnectionName().c_str());  // debug

    setEventLoop(NULL);

    if (tcpServer_)
        tcpServer_->decConnCount();
    TcpInspectInfo::instance().tcpConnDestroyCount.increment();
}

//-----------------------------------------------------------------------------

void TcpConnection::init()
{
    tcpServer_ = NULL;
    eventLoop_ = NULL;
    isErrorOccurred_ = false;
}

//-----------------------------------------------------------------------------
// ����: �ύһ���������� (�̰߳�ȫ)
// ����:
//   timeout - ��ʱֵ (����)
//-----------------------------------------------------------------------------
void TcpConnection::send(const void *buffer, size_t size, const Context& context, int timeout)
{
    if (!buffer || size <= 0) return;

    if (eventLoop_ == NULL)
        iseThrowException(SEM_EVENT_LOOP_NOT_SPECIFIED);

    if (getEventLoop()->isInLoopThread())
        postSendTask(buffer, static_cast<int>(size), context, timeout);
    else
    {
        string data((const char*)buffer, size);
        getEventLoop()->delegateToLoop(
            boost::bind(&TcpConnection::postSendTask, this, data, context, timeout));
    }
}

//-----------------------------------------------------------------------------
// ����: �ύһ���������� (�̰߳�ȫ)
// ����:
//   timeout - ��ʱֵ (����)
//-----------------------------------------------------------------------------
void TcpConnection::recv(const PacketSplitter& packetSplitter, const Context& context, int timeout)
{
    if (!packetSplitter) return;

    if (eventLoop_ == NULL)
        iseThrowException(SEM_EVENT_LOOP_NOT_SPECIFIED);

    if (getEventLoop()->isInLoopThread())
        postRecvTask(packetSplitter, context, timeout);
    else
    {
        getEventLoop()->delegateToLoop(
            boost::bind(&TcpConnection::postRecvTask, this, packetSplitter, context, timeout));
    }
}

//-----------------------------------------------------------------------------

const string& TcpConnection::getConnectionName() const
{
    if (connectionName_.empty() && isConnected())
    {
        static Mutex mutex;
        static SeqNumberAlloc connIdAlloc_;

        AutoLocker locker(mutex);

        connectionName_ = formatString("%s-%s#%s",
            getSocket().getLocalAddr().getDisplayStr().c_str(),
            getSocket().getPeerAddr().getDisplayStr().c_str(),
            intToStr((INT64)connIdAlloc_.allocId()).c_str());
    }

    return connectionName_;
}

//-----------------------------------------------------------------------------

int TcpConnection::getServerIndex() const
{
    return tcpServer_ ? boost::any_cast<int>(tcpServer_->getContext()) : -1;
}

//-----------------------------------------------------------------------------

int TcpConnection::getServerPort() const
{
    return tcpServer_ ? tcpServer_->getLocalPort() : 0;
}

//-----------------------------------------------------------------------------

int TcpConnection::getServerConnCount() const
{
    return tcpServer_ ? tcpServer_->getConnectionCount() : 0;
}

//-----------------------------------------------------------------------------
// ����: �Ͽ�����
// ��ע:
//   ���ֱ�� close socket��Linux�� EPoll ���������֪ͨ���Ӷ��޷��������
//   �� shutdown ��û�����⡣�� Windows �£������� close ���� shutdown��
//   ֻҪ�����ϴ��ڽ��ջ��Ͷ�����IOCP �����Բ������
//-----------------------------------------------------------------------------
void TcpConnection::doDisconnect()
{
    shutdown(true, false);
}

//-----------------------------------------------------------------------------
// ����: ���ӷ����˴���
// ��ע:
//   �����ڷ����������û�����ü�ֵ��Ӧ�����٣�����Ӧ�������٣���Ϊ�Ժ�ִ�е�
//   ί�и��¼�ѭ���ķº����п��ܴ��ڶԴ����ӵĻص������ԣ�Ӧ���� addFinalizer()
//   �ķ�ʽ���ٴν����ٶ��������ί�и��¼�ѭ������ÿ��ѭ����ĩβִ�С�
//-----------------------------------------------------------------------------
void TcpConnection::errorOccurred()
{
    if (isErrorOccurred_) return;
    isErrorOccurred_ = true;

    TcpInspectInfo::instance().errorOccurredCount.increment();

    shutdown(true, true);

    ISE_ASSERT(eventLoop_ != NULL);

    getEventLoop()->executeInLoop(boost::bind(
        &IseBusiness::onTcpDisconnected,
        &iseApp().iseBusiness(), shared_from_this()));

    // setEventLoop(NULL) ��ʹ shared_ptr<TcpConnection> �������ü������������ٶ���
    getEventLoop()->addFinalizer(boost::bind(
        &TcpConnection::setEventLoop,
        shared_from_this(),   // ��֤�ڵ��� setEventLoop(NULL) �ڼ䲻������ TcpConnection ����
        (TcpEventLoop*)NULL));
}

//-----------------------------------------------------------------------------
// ����: �����պͷ��������Ƿ�ʱ
//-----------------------------------------------------------------------------
void TcpConnection::checkTimeout(UINT curTicks)
{
    if (!sendTaskQueue_.empty())
    {
        SendTask& task = sendTaskQueue_.front();

        if (task.startTicks == 0)
            task.startTicks = curTicks;
        else
        {
            if (task.timeout > 0 &&
                (int)getTickDiff(task.startTicks, curTicks) > task.timeout)
            {
                shutdown(true, true);
            }
        }
    }

    if (!recvTaskQueue_.empty())
    {
        RecvTask& task = recvTaskQueue_.front();

        if (task.startTicks == 0)
            task.startTicks = curTicks;
        else
        {
            if (task.timeout > 0 &&
                (int)getTickDiff(task.startTicks, curTicks) > task.timeout)
            {
                shutdown(true, true);
            }
        }
    }
}

//-----------------------------------------------------------------------------

void TcpConnection::postSendTask(const string& data, const Context& context, int timeout)
{
    postSendTask(data.c_str(), (int)data.size(), context, timeout);
}

//-----------------------------------------------------------------------------
// ����: ���ô����Ӵ������ĸ� eventLoop
//-----------------------------------------------------------------------------
void TcpConnection::setEventLoop(TcpEventLoop *eventLoop)
{
    if (eventLoop != eventLoop_)
    {
        if (eventLoop_)
        {
            TcpEventLoop *temp = eventLoop_;
            eventLoop_ = NULL;
            temp->removeConnection(this);
            eventLoopChanged();
        }

        if (eventLoop)
        {
            eventLoop->assertInLoopThread();
            eventLoop_ = eventLoop;
            eventLoop->addConnection(this);
            eventLoopChanged();
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// class TcpClient

BaseTcpConnection* TcpClient::createConnection()
{
    BaseTcpConnection *result = NULL;

#ifdef ISE_WINDOWS
    result = new WinTcpConnection();
#endif
#ifdef ISE_LINUX
    result = new LinuxTcpConnection();
#endif

    return result;
}

//-----------------------------------------------------------------------------
// ����: �� connection �ҽӵ� EventLoop ��
// ����:
//   index - EventLoop ����� (0-based)��Ϊ -1 ��ʾ�Զ�ѡ��
// ��ע:
//   �ҽӳɹ���TcpClient ���ͷŶ� TcpConnection �Ŀ���Ȩ��
//-----------------------------------------------------------------------------
bool TcpClient::registerToEventLoop(int index)
{
    bool result = false;

    if (connection_ != NULL)
    {
        TcpEventLoopList& eventLoopList = iseApp().mainServer().getMainTcpServer().getTcpClientEventLoopList();
        result = eventLoopList.registerToEventLoop(connection_, index);
        if (result)
            connection_ = NULL;
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
// class TcpServer

TcpServer::TcpServer(int eventLoopCount) :
    eventLoopList_(eventLoopCount)
{
    // nothing
}

//-----------------------------------------------------------------------------

void TcpServer::open()
{
    BaseTcpServer::open();
    eventLoopList_.start();
}

//-----------------------------------------------------------------------------

void TcpServer::close()
{
    eventLoopList_.stop();
    BaseTcpServer::close();
}

//-----------------------------------------------------------------------------
// ����: �������Ӷ���
//-----------------------------------------------------------------------------
BaseTcpConnection* TcpServer::createConnection(SOCKET socketHandle)
{
    BaseTcpConnection *result = NULL;

#ifdef ISE_WINDOWS
    result = new WinTcpConnection(this, socketHandle);
#endif
#ifdef ISE_LINUX
    result = new LinuxTcpConnection(this, socketHandle);
#endif

    return result;
}

//-----------------------------------------------------------------------------
// ����: �յ�����
// ע��:
//   1. �˻ص���TCP�����������߳�(TcpListenerThread)��ִ�С�
//   2. �� connection->setEventLoop() �ĵ������¼�ѭ���߳���ִ�С�
//   3. Ϊ��������˳�ʱ�ܾ������ʣ�����ӣ���ʱ�����������������롣
//-----------------------------------------------------------------------------
void TcpServer::acceptConnection(BaseTcpConnection *connection)
{
    if (!iseApp().isTerminated())
        eventLoopList_.registerToEventLoop(connection, -1);
    else
        delete connection;
}

///////////////////////////////////////////////////////////////////////////////
// class TcpConnector

TcpConnector::TcpConnector() :
    taskList_(false, true),
    thread_(NULL)
{
    // nothing
}

TcpConnector::~TcpConnector()
{
    stop();
    clear();
}

//-----------------------------------------------------------------------------

void TcpConnector::connect(const InetAddress& peerAddr,
    const CompleteCallback& completeCallback, const Context& context)
{
    AutoLocker locker(mutex_);

    TaskItem *item = new TaskItem();
    item->peerAddr = peerAddr;
    item->completeCallback = completeCallback;
    item->state = ACS_NONE;
    item->context = context;

    taskList_.add(item);
    start();
}

//-----------------------------------------------------------------------------

void TcpConnector::clear()
{
    AutoLocker locker(mutex_);
    taskList_.clear();
}

//-----------------------------------------------------------------------------

void TcpConnector::start()
{
    if (thread_ == NULL)
    {
        thread_ = new WorkerThread(*this);
        thread_->setAutoDelete(true);
        thread_->run();
    }
}

//-----------------------------------------------------------------------------

void TcpConnector::stop()
{
    if (thread_ != NULL)
    {
        thread_->terminate();
        thread_->waitFor();
        thread_ = NULL;
    }
}

//-----------------------------------------------------------------------------

void TcpConnector::work(WorkerThread& thread)
{
    while (!thread.isTerminated() && !taskList_.isEmpty())
    {
        tryConnect();

        int fromIndex = 0;
        FdList fds, connectedFds, failedFds;

        getPendingFdsFromTaskList(fromIndex, fds);
        checkAsyncConnectState(fds, connectedFds, failedFds);

        if (fromIndex >= taskList_.getCount())
            fromIndex = 0;

        for (int i = 0; i < (int)connectedFds.size(); ++i)
        {
            TaskItem *task = findTask(connectedFds[i]);
            if (task != NULL)
            {
                task->state = ACS_CONNECTED;
                task->tcpClient.getConnection().setContext(task->context);
            }
        }

        for (int i = 0; i < (int)failedFds.size(); ++i)
        {
            TaskItem *task = findTask(failedFds[i]);
            if (task != NULL)
            {
                task->state = ACS_FAILED;
                task->tcpClient.disconnect();
            }
        }

        invokeCompleteCallback();
    }
}

//-----------------------------------------------------------------------------

void TcpConnector::tryConnect()
{
    AutoLocker locker(mutex_);

    for (int i = 0; i < taskList_.getCount(); ++i)
    {
        TaskItem *task = taskList_[i];
        if (task->state == ACS_NONE)
        {
            task->state = (ASYNC_CONNECT_STATE)task->tcpClient.asyncConnect(
                ipToString(task->peerAddr.ip), task->peerAddr.port, 0);
        }
    }
}

//-----------------------------------------------------------------------------

void TcpConnector::getPendingFdsFromTaskList(int& fromIndex, FdList& fds)
{
    AutoLocker locker(mutex_);

    fds.clear();
    for (int i = fromIndex; i < taskList_.getCount(); ++i)
    {
        TaskItem *task = taskList_[i];
        if (task->state == ACS_CONNECTING)
        {
            fds.push_back(task->tcpClient.getConnection().getSocket().getHandle());
            if (fds.size() >= FD_SETSIZE)
            {
                fromIndex = i + 1;
                break;
            }
        }
    }
}

//-----------------------------------------------------------------------------

void TcpConnector::checkAsyncConnectState(const FdList& fds,
    FdList& connectedFds, FdList& failedFds)
{
    const int WAIT_TIME = 1;  // ms

        fd_set rset, wset, eset;
    struct timeval tv;

    connectedFds.clear();
    failedFds.clear();

    // init wset & rset
    FD_ZERO(&rset);
    for (int i = 0; i < (int)fds.size(); ++i)
        FD_SET(fds[i], &rset);
    wset = rset;
        eset = rset;

    // find max fd
    SOCKET maxFd = 0;
    for (int i = 0; i < (int)fds.size(); ++i)
    {
        if (maxFd < fds[i])
            maxFd = fds[i];
    }

    tv.tv_sec = 0;
    tv.tv_usec = WAIT_TIME * 1000;

        int r = select(maxFd + 1, &rset, &wset, &eset, &tv);
    if (r > 0)
    {
        for (int i = 0; i < (int)fds.size(); ++i)
        {
            int state = ACS_CONNECTING;
            SOCKET fd = fds[i];
                if (FD_ISSET(fd, &rset) || FD_ISSET(fd, &wset) || FD_ISSET(fd, &eset))
            {
                socklen_t errLen = sizeof(int);
                int errorCode = 0;
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&errorCode, &errLen) < 0 || errorCode)
                    state = ACS_FAILED;
                else
                    state = ACS_CONNECTED;
            }

            if (state == ACS_CONNECTED)
                connectedFds.push_back(fd);
            else if (state == ACS_FAILED)
                failedFds.push_back(fd);
        }
    }
}

//-----------------------------------------------------------------------------

TcpConnector::TaskItem* TcpConnector::findTask(SOCKET fd)
{
    TaskItem *result = NULL;

    for (int i = 0; i < taskList_.getCount(); ++i)
    {
        TaskItem *task = taskList_[i];
        if (task->tcpClient.getConnection().getSocket().getHandle() == fd)
        {
            result = task;
            break;
        }
    }

    return result;
}

//-----------------------------------------------------------------------------

void TcpConnector::invokeCompleteCallback()
{
    TaskList completeList(false, true);

    {
        AutoLocker locker(mutex_);

        for (int i = 0; i < taskList_.getCount(); ++i)
        {
            TaskItem *task = taskList_[i];
            if (task->state == ACS_CONNECTED || task->state == ACS_FAILED)
            {
                taskList_.extract(i);
                completeList.add(task);
            }
        }
    }

    for (int i = 0; i < completeList.getCount(); ++i)
    {
        TaskItem *task = completeList[i];
        if (task->completeCallback)
        {
            bool success = (task->state == ACS_CONNECTED);

            task->completeCallback(success,
                success ? &task->tcpClient.getConnection() : NULL,
                task->peerAddr, task->context);

            if (success)
                task->tcpClient.registerToEventLoop();
        }
    }
}

///////////////////////////////////////////////////////////////////////////////

#ifdef ISE_WINDOWS

///////////////////////////////////////////////////////////////////////////////
// class WinTcpConnection

WinTcpConnection::WinTcpConnection()
{
    init();
}

WinTcpConnection::WinTcpConnection(TcpServer *tcpServer, SOCKET socketHandle) :
    TcpConnection(tcpServer, socketHandle)
{
    init();
}

//-----------------------------------------------------------------------------

void WinTcpConnection::init()
{
    isSending_ = false;
    isRecving_ = false;
    bytesSent_ = 0;
    bytesRecved_ = 0;
}

//-----------------------------------------------------------------------------

void WinTcpConnection::eventLoopChanged()
{
    if (getEventLoop() != NULL)
    {
        getEventLoop()->assertInLoopThread();
        tryRecv();
    }
}

//-----------------------------------------------------------------------------
// ����: �ύһ����������
//-----------------------------------------------------------------------------
void WinTcpConnection::postSendTask(const void *buffer, int size,
    const Context& context, int timeout)
{
    sendBuffer_.append(buffer, size);

    SendTask task;
    task.bytes = size;
    task.context = context;
    task.timeout = timeout;

    sendTaskQueue_.push_back(task);

    trySend();
}

//-----------------------------------------------------------------------------
// ����: �ύһ����������
//-----------------------------------------------------------------------------
void WinTcpConnection::postRecvTask(const PacketSplitter& packetSplitter,
    const Context& context, int timeout)
{
    RecvTask task;
    task.packetSplitter = packetSplitter;
    task.context = context;
    task.timeout = timeout;

    recvTaskQueue_.push_back(task);

    tryRecv();
}

//-----------------------------------------------------------------------------

void WinTcpConnection::trySend()
{
    if (isSending_) return;

    int readableBytes = sendBuffer_.getReadableBytes();
    if (readableBytes > 0)
    {
        const int MAX_SEND_SIZE = 1024*32;

        const char *buffer = sendBuffer_.peek();
        int sendSize = ise::min(readableBytes, MAX_SEND_SIZE);

        isSending_ = true;
        getEventLoop()->getIocpObject()->send(
            getSocket().getHandle(),
            (PVOID)buffer, sendSize, 0,
            boost::bind(&WinTcpConnection::onIocpCallback, shared_from_this(), _1),
            this, EMPTY_CONTEXT);
    }
}

//-----------------------------------------------------------------------------

void WinTcpConnection::tryRecv()
{
    if (isRecving_) return;

    const int MAX_BUFFER_SIZE = iseApp().iseOptions().getTcpMaxRecvBufferSize();
    const int MAX_RECV_SIZE = 1024*16;

    if (recvTaskQueue_.empty() && recvBuffer_.getReadableBytes() >= MAX_BUFFER_SIZE)
        return;

    isRecving_ = true;
    recvBuffer_.append(MAX_RECV_SIZE);
    const char *buffer = recvBuffer_.peek() + bytesRecved_;

    getEventLoop()->getIocpObject()->recv(
        getSocket().getHandle(),
        (PVOID)buffer, MAX_RECV_SIZE, 0,
        boost::bind(&WinTcpConnection::onIocpCallback, shared_from_this(), _1),
        this, EMPTY_CONTEXT);
}

//-----------------------------------------------------------------------------

void WinTcpConnection::onIocpCallback(const TcpConnectionPtr& thisObj, const IocpTaskData& taskData)
{
    WinTcpConnection *thisPtr = static_cast<WinTcpConnection*>(thisObj.get());

    if (taskData.getErrorCode() == 0)
    {
        switch (taskData.getTaskType())
        {
        case ITT_SEND:
            thisPtr->onSendCallback(taskData);
            break;
        case ITT_RECV:
            thisPtr->onRecvCallback(taskData);
            break;
        }
    }
    else
    {
        thisPtr->errorOccurred();
    }
}

//-----------------------------------------------------------------------------

void WinTcpConnection::onSendCallback(const IocpTaskData& taskData)
{
    ISE_ASSERT(taskData.getErrorCode() == 0);
        if (this->isErrorOccurred_ || eventLoop_ == NULL) 
        {
            return;
        }

    if (taskData.getBytesTrans() < taskData.getDataSize())
    {
        getEventLoop()->getIocpObject()->send(
            (SOCKET)taskData.getFileHandle(),
            taskData.getEntireDataBuf(),
            taskData.getEntireDataSize(),
            taskData.getDataBuf() - taskData.getEntireDataBuf() + taskData.getBytesTrans(),
            taskData.getCallback(), taskData.getCaller(), taskData.getContext());
    }
    else
    {
        isSending_ = false;
        sendBuffer_.retrieve(taskData.getEntireDataSize());
    }

    bytesSent_ += taskData.getBytesTrans();

    while (!sendTaskQueue_.empty())
    {
        SendTask& task = sendTaskQueue_.front();
        if (bytesSent_ >= task.bytes)
        {
            bytesSent_ -= task.bytes;
            iseApp().iseBusiness().onTcpSendComplete(shared_from_this(), task.context);
            sendTaskQueue_.pop_front();
        }
        else
            break;
    }

    if (!sendTaskQueue_.empty())
        trySend();
}

//-----------------------------------------------------------------------------

void WinTcpConnection::onRecvCallback(const IocpTaskData& taskData)
{
    ISE_ASSERT(taskData.getErrorCode() == 0);

    if (taskData.getBytesTrans() < taskData.getDataSize())
    {
        getEventLoop()->getIocpObject()->recv(
            (SOCKET)taskData.getFileHandle(),
            taskData.getEntireDataBuf(),
            taskData.getEntireDataSize(),
            taskData.getDataBuf() - taskData.getEntireDataBuf() + taskData.getBytesTrans(),
            taskData.getCallback(), taskData.getCaller(), taskData.getContext());
    }
    else
    {
        isRecving_ = false;
    }

    bytesRecved_ += taskData.getBytesTrans();

    while (!recvTaskQueue_.empty())
    {
        RecvTask& task = recvTaskQueue_.front();
        const char *buffer = recvBuffer_.peek();
        bool packetRecved = false;

        if (bytesRecved_ > 0)
        {
            int packetSize = 0;
            task.packetSplitter(buffer, bytesRecved_, packetSize);
            if (packetSize > 0)
            {
                bytesRecved_ -= packetSize;
                iseApp().iseBusiness().onTcpRecvComplete(shared_from_this(),
                    (void*)buffer, packetSize, task.context);
                recvTaskQueue_.pop_front();
                recvBuffer_.retrieve(packetSize);
                packetRecved = true;
            }
        }

        if (!packetRecved)
            break;
    }

    tryRecv();
}

///////////////////////////////////////////////////////////////////////////////
// class IocpTaskData

IocpTaskData::IocpTaskData() :
    iocpHandle_(INVALID_HANDLE_VALUE),
    fileHandle_(INVALID_HANDLE_VALUE),
    taskType_((IOCP_TASK_TYPE)0),
    taskSeqNum_(0),
    caller_(0),
    entireDataBuf_(0),
    entireDataSize_(0),
    bytesTrans_(0),
    errorCode_(0)
{
    wsaBuffer_.buf = NULL;
    wsaBuffer_.len = 0;
}

///////////////////////////////////////////////////////////////////////////////
// class IocpBufferAllocator

IocpBufferAllocator::IocpBufferAllocator(int bufferSize) :
    bufferSize_(bufferSize),
    usedCount_(0)
{
    // nothing
}

//-----------------------------------------------------------------------------

IocpBufferAllocator::~IocpBufferAllocator()
{
    clear();
}

//-----------------------------------------------------------------------------

PVOID IocpBufferAllocator::allocBuffer()
{
    AutoLocker locker(mutex_);
    PVOID result;

    if (!items_.isEmpty())
    {
        result = items_.last();
        items_.del(items_.getCount() - 1);
    }
    else
    {
        result = new char[bufferSize_];
        if (result == NULL)
            iseThrowMemoryException();
    }

    usedCount_++;
    return result;
}

//-----------------------------------------------------------------------------

void IocpBufferAllocator::returnBuffer(PVOID buffer)
{
    AutoLocker locker(mutex_);

    if (buffer != NULL && items_.indexOf(buffer) == -1)
    {
        items_.add(buffer);
        usedCount_--;
    }
}

//-----------------------------------------------------------------------------

void IocpBufferAllocator::clear()
{
    AutoLocker locker(mutex_);

    for (int i = 0; i < items_.getCount(); i++)
        delete[] (char*)items_[i];
    items_.clear();
}

///////////////////////////////////////////////////////////////////////////////
// class IocpPendingCounter

void IocpPendingCounter::inc(PVOID caller, IOCP_TASK_TYPE taskType)
{
    AutoLocker locker(mutex_);

    Items::iterator iter = items_.find(caller);
    if (iter == items_.end())
    {
        CountData Data = {0, 0};
        iter = items_.insert(std::make_pair(caller, Data)).first;
    }

    if (taskType == ITT_SEND)
        iter->second.sendCount++;
    else if (taskType == ITT_RECV)
        iter->second.recvCount++;
}

//-----------------------------------------------------------------------------

void IocpPendingCounter::dec(PVOID caller, IOCP_TASK_TYPE taskType)
{
    AutoLocker locker(mutex_);

    Items::iterator iter = items_.find(caller);
    if (iter != items_.end())
    {
        if (taskType == ITT_SEND)
            iter->second.sendCount--;
        else if (taskType == ITT_RECV)
            iter->second.recvCount--;

        if (iter->second.sendCount <= 0 && iter->second.recvCount <= 0)
            items_.erase(iter);
    }
}

//-----------------------------------------------------------------------------

int IocpPendingCounter::get(PVOID caller)
{
    AutoLocker locker(mutex_);

    Items::iterator iter = items_.find(caller);
    if (iter == items_.end())
        return 0;
    else
        return ise::max(0, iter->second.sendCount + iter->second.recvCount);
}

//-----------------------------------------------------------------------------

int IocpPendingCounter::get(IOCP_TASK_TYPE taskType)
{
    AutoLocker locker(mutex_);

    int result = 0;
    if (taskType == ITT_SEND)
    {
        for (Items::iterator iter = items_.begin(); iter != items_.end(); ++iter)
            result += iter->second.sendCount;
    }
    else if (taskType == ITT_RECV)
    {
        for (Items::iterator iter = items_.begin(); iter != items_.end(); ++iter)
            result += iter->second.recvCount;
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
// class IocpObject

IocpBufferAllocator IocpObject::bufferAlloc_(sizeof(IocpOverlappedData));
SeqNumberAlloc IocpObject::taskSeqAlloc_(0);
IocpPendingCounter IocpObject::pendingCounter_;

//-----------------------------------------------------------------------------

IocpObject::IocpObject() :
    iocpHandle_(0)
{
    initialize();
}

//-----------------------------------------------------------------------------

IocpObject::~IocpObject()
{
    finalize();
}

//-----------------------------------------------------------------------------

bool IocpObject::associateHandle(SOCKET socketHandle)
{
    HANDLE h = ::CreateIoCompletionPort((HANDLE)socketHandle, iocpHandle_, 0, 0);
        ISE_ASSERT(h != NULL);
    return (h != 0);
}

//-----------------------------------------------------------------------------

bool IocpObject::isComplete(PVOID caller)
{
    return (pendingCounter_.get(caller) <= 0);
}

//-----------------------------------------------------------------------------

void IocpObject::work()
{
    /*
    FROM MSDN:

    If the function dequeues a completion packet for a successful I/O operation from the completion port,
    the return value is nonzero. The function stores information in the variables pointed to by the
    lpNumberOfBytes, lpCompletionKey, and lpOverlapped parameters.

    If *lpOverlapped is NULL and the function does not dequeue a completion packet from the completion port,
    the return value is zero. The function does not store information in the variables pointed to by the
    lpNumberOfBytes and lpCompletionKey parameters. To get extended error information, call GetLastError.
    If the function did not dequeue a completion packet because the wait timed out, GetLastError returns
    WAIT_TIMEOUT.

    If *lpOverlapped is not NULL and the function dequeues a completion packet for a failed I/O operation
    from the completion port, the return value is zero. The function stores information in the variables
    pointed to by lpNumberOfBytes, lpCompletionKey, and lpOverlapped. To get extended error information,
    call GetLastError.

    If a socket handle associated with a completion port is closed, GetQueuedCompletionStatus returns
    ERROR_SUCCESS (0), with *lpOverlapped non-NULL and lpNumberOfBytes equal zero.
    */

    const int IOCP_WAIT_TIMEOUT = 1000*1;  // ms

        while (true)
        {
            IocpOverlappedData *overlappedPtr = NULL;
            DWORD bytesTransferred = 0;
            ULONG_PTR  nTemp = 0;
            int errorCode = 0;

            struct AutoFinalizer
            {
            private:
                IocpObject& iocpObject_;
            IocpOverlappedData*& ovPtr_;
        public:
            AutoFinalizer(IocpObject& iocpObject, IocpOverlappedData*& ovPtr) :
                iocpObject_(iocpObject), ovPtr_(ovPtr) {}
            ~AutoFinalizer()
            {
                if (ovPtr_)
                {
                    iocpObject_.pendingCounter_.dec(
                        ovPtr_->taskData.getCaller(),
                        ovPtr_->taskData.getTaskType());
                    iocpObject_.destroyOverlappedData(ovPtr_);
                }
            }
        } finalizer(*this, overlappedPtr);

        if (::GetQueuedCompletionStatus(iocpHandle_, &bytesTransferred, &nTemp,
            (LPOVERLAPPED*)&overlappedPtr, IOCP_WAIT_TIMEOUT))
        {
            if (overlappedPtr != NULL && bytesTransferred == 0)
            {
                errorCode = overlappedPtr->taskData.getErrorCode();
                if (errorCode == 0)
                    errorCode = GetLastError();
                if (errorCode == 0)
                    errorCode = SOCKET_ERROR;
            }
        }
        else
        {
            if (overlappedPtr != NULL)
                errorCode = GetLastError();
            else
            {
                if (GetLastError() != WAIT_TIMEOUT)
                    throwGeneralError();
            }
        }

        if (overlappedPtr != NULL)
        {
            IocpTaskData *taskPtr = &overlappedPtr->taskData;
            taskPtr->bytesTrans_ = bytesTransferred;
            if (taskPtr->errorCode_ == 0)
                taskPtr->errorCode_ = errorCode;

            invokeCallback(*taskPtr);
        }
        else
            break;
    }
}

//-----------------------------------------------------------------------------

void IocpObject::wakeup()
{
    ::PostQueuedCompletionStatus(iocpHandle_, 0, 0, NULL);
}

//-----------------------------------------------------------------------------

void IocpObject::send(SOCKET socketHandle, PVOID buffer, int size, int offset,
    const IocpCallback& callback, PVOID caller, const Context& context)
{
    IocpOverlappedData *ovDataPtr;
    IocpTaskData *taskPtr;
    DWORD numberOfBytesSent;

    pendingCounter_.inc(caller, ITT_SEND);

    ovDataPtr = createOverlappedData(ITT_SEND, (HANDLE)socketHandle, buffer, size,
        offset, callback, caller, context);
    taskPtr = &(ovDataPtr->taskData);

    if (::WSASend(socketHandle, &taskPtr->wsaBuffer_, 1, &numberOfBytesSent, 0,
        (LPWSAOVERLAPPED)ovDataPtr, NULL) == SOCKET_ERROR)
    {
        if (GetLastError() != ERROR_IO_PENDING)
            postError(GetLastError(), ovDataPtr);
    }
}

//-----------------------------------------------------------------------------

void IocpObject::recv(SOCKET socketHandle, PVOID buffer, int size, int offset,
    const IocpCallback& callback, PVOID caller, const Context& context)
{
    IocpOverlappedData *ovDataPtr;
    IocpTaskData *taskPtr;
    DWORD nNumberOfBytesRecvd, flags = 0;

    pendingCounter_.inc(caller, ITT_RECV);

    ovDataPtr = createOverlappedData(ITT_RECV, (HANDLE)socketHandle, buffer, size,
        offset, callback, caller, context);
    taskPtr = &(ovDataPtr->taskData);

    if (::WSARecv(socketHandle, &taskPtr->wsaBuffer_, 1, &nNumberOfBytesRecvd, &flags,
        (LPWSAOVERLAPPED)ovDataPtr, NULL) == SOCKET_ERROR)
    {
        if (GetLastError() != ERROR_IO_PENDING)
            postError(GetLastError(), ovDataPtr);
    }
}

//-----------------------------------------------------------------------------

void IocpObject::initialize()
{
    iocpHandle_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 1);
    if (iocpHandle_ == 0)
        throwGeneralError();
}

//-----------------------------------------------------------------------------

void IocpObject::finalize()
{
    CloseHandle(iocpHandle_);
    iocpHandle_ = 0;
}

//-----------------------------------------------------------------------------

void IocpObject::throwGeneralError()
{
    iseThrowException(formatString(SEM_IOCP_ERROR, GetLastError()).c_str());
}

//-----------------------------------------------------------------------------

IocpOverlappedData* IocpObject::createOverlappedData(IOCP_TASK_TYPE taskType,
    HANDLE fileHandle, PVOID buffer, int size, int offset,
    const IocpCallback& callback, PVOID caller, const Context& context)
{
    ISE_ASSERT(buffer != NULL);
    ISE_ASSERT(size >= 0);
    ISE_ASSERT(offset >= 0);
    ISE_ASSERT(offset < size);

    IocpOverlappedData *result = (IocpOverlappedData*)bufferAlloc_.allocBuffer();
    memset(result, 0, sizeof(*result));

    result->taskData.iocpHandle_ = iocpHandle_;
    result->taskData.fileHandle_ = fileHandle;
    result->taskData.taskType_ = taskType;
    result->taskData.taskSeqNum_ = taskSeqAlloc_.allocId();
    result->taskData.caller_ = caller;
    result->taskData.context_ = context;
    result->taskData.entireDataBuf_ = buffer;
    result->taskData.entireDataSize_ = size;
    result->taskData.wsaBuffer_.buf = (char*)buffer + offset;
    result->taskData.wsaBuffer_.len = size - offset;
    result->taskData.callback_ = callback;

    return result;
}

//-----------------------------------------------------------------------------

void IocpObject::destroyOverlappedData(IocpOverlappedData *ovDataPtr)
{
    // ����Ҫ��ovDataPtr->taskData �еĶ�����Ҫ������
    // ���� taskData.callback_ �г��� TcpConnection �� shared_ptr��
    ovDataPtr->~IocpOverlappedData();

    bufferAlloc_.returnBuffer(ovDataPtr);
}

//-----------------------------------------------------------------------------

void IocpObject::postError(int errorCode, IocpOverlappedData *ovDataPtr)
{
    ovDataPtr->taskData.errorCode_ = errorCode;
    ::PostQueuedCompletionStatus(iocpHandle_, 0, 0, LPOVERLAPPED(ovDataPtr));
}

//-----------------------------------------------------------------------------

void IocpObject::invokeCallback(const IocpTaskData& taskData)
{
    const IocpCallback& callback = taskData.getCallback();
    if (callback)
        callback(taskData);
}

    ///////////////////////////////////////////////////////////////////////////////
    // class WinTcpEventLoop

    WinTcpEventLoop::WinTcpEventLoop()
    {
        iocpObject_ = new IocpObject();
        _ASSERT(NULL != iocpObject_);
    }

    WinTcpEventLoop::~WinTcpEventLoop()
    {
        stop(false, true);
        if (NULL != iocpObject_)
            delete iocpObject_;
    }

    //-----------------------------------------------------------------------------
    // ����: ִ�е����¼�ѭ���еĹ���
    //-----------------------------------------------------------------------------
void WinTcpEventLoop::doLoopWork(Thread *thread)
{
    iocpObject_->work();
}

//-----------------------------------------------------------------------------
// ����: �����¼�ѭ���е���������
//-----------------------------------------------------------------------------
void WinTcpEventLoop::wakeupLoop()
{
    iocpObject_->wakeup();
}

//-----------------------------------------------------------------------------
// ����: ��������ע�ᵽ�¼�ѭ����
//-----------------------------------------------------------------------------
void WinTcpEventLoop::registerConnection(TcpConnection *connection)
{
    iocpObject_->associateHandle(connection->getSocket().getHandle());
}

//-----------------------------------------------------------------------------
// ����: ���¼�ѭ����ע������
//-----------------------------------------------------------------------------
void WinTcpEventLoop::unregisterConnection(TcpConnection *connection)
{
    // nothing
}

///////////////////////////////////////////////////////////////////////////////

#endif  /* ifdef ISE_WINDOWS */

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

#ifdef ISE_LINUX

///////////////////////////////////////////////////////////////////////////////
// class LinuxTcpConnection

LinuxTcpConnection::LinuxTcpConnection()
{
    init();
}

LinuxTcpConnection::LinuxTcpConnection(TcpServer *tcpServer, SOCKET socketHandle) :
    TcpConnection(tcpServer, socketHandle)
{
    init();
}

//-----------------------------------------------------------------------------

void LinuxTcpConnection::init()
{
    bytesSent_ = 0;
    enableSend_ = false;
    enableRecv_ = false;
}

//-----------------------------------------------------------------------------

void LinuxTcpConnection::eventLoopChanged()
{
    if (getEventLoop() != NULL)
    {
        getEventLoop()->assertInLoopThread();
        setRecvEnabled(true);
    }
}

//-----------------------------------------------------------------------------
// ����: �ύһ����������
//-----------------------------------------------------------------------------
void LinuxTcpConnection::postSendTask(const void *buffer, int size,
    const Context& context, int timeout)
{
    sendBuffer_.append(buffer, size);

    SendTask task;
    task.bytes = size;
    task.context = context;
    task.timeout = timeout;

    sendTaskQueue_.push_back(task);

    if (!enableSend_)
        setSendEnabled(true);
}

//-----------------------------------------------------------------------------
// ����: �ύһ����������
//-----------------------------------------------------------------------------
void LinuxTcpConnection::postRecvTask(const PacketSplitter& packetSplitter,
    const Context& context, int timeout)
{
    RecvTask task;
    task.packetSplitter = packetSplitter;
    task.context = context;
    task.timeout = timeout;

    recvTaskQueue_.push_back(task);

    if (!enableRecv_)
        setRecvEnabled(true);

    // ע��: �˴�������� tryRetrievePacket()���������ɽ�����ֹ�����ǣ�ֱ��
    // ���� tryRetrievePacket() �ֻ����ѭ�����ã����Ա����� delegateToLoop()��
    // �ұ���ʹ�� shared_from_this()��������ĳЩ����»ᵼ�³��������
    getEventLoop()->delegateToLoop(boost::bind(
        &LinuxTcpConnection::afterPostRecvTask, shared_from_this()));
}

//-----------------------------------------------------------------------------
// ����: ���á��Ƿ���ӿɷ����¼���
//-----------------------------------------------------------------------------
void LinuxTcpConnection::setSendEnabled(bool enabled)
{
    ISE_ASSERT(eventLoop_ != NULL);

    enableSend_ = enabled;
    getEventLoop()->updateConnection(this, enableSend_, enableRecv_);
}

//-----------------------------------------------------------------------------
// ����: ���á��Ƿ���ӿɽ����¼���
//-----------------------------------------------------------------------------
void LinuxTcpConnection::setRecvEnabled(bool enabled)
{
    ISE_ASSERT(eventLoop_ != NULL);

    enableRecv_ = enabled;
    getEventLoop()->updateConnection(this, enableSend_, enableRecv_);
}

//-----------------------------------------------------------------------------
// ����: �����ɷ��͡��¼�����ʱ�����Է�������
//-----------------------------------------------------------------------------
void LinuxTcpConnection::trySend()
{
    int readableBytes = sendBuffer_.getReadableBytes();
    if (readableBytes <= 0)
    {
        setSendEnabled(false);
        return;
    }

    const char *buffer = sendBuffer_.peek();
    int bytesSent = sendBuffer((void*)buffer, readableBytes, false);
    if (bytesSent < 0)
    {
        errorOccurred();
        return;
    }

    if (bytesSent > 0)
    {
        sendBuffer_.retrieve(bytesSent);
        bytesSent_ += bytesSent;

        while (!sendTaskQueue_.empty())
        {
            SendTask& task = sendTaskQueue_.front();
            if (bytesSent_ >= task.bytes)
            {
                bytesSent_ -= task.bytes;
                iseApp().iseBusiness().onTcpSendComplete(shared_from_this(), task.context);
                sendTaskQueue_.pop_front();
            }
            else
                break;
        }
    }
}

//-----------------------------------------------------------------------------
// ����: �����ɽ��ա��¼�����ʱ�����Խ�������
//-----------------------------------------------------------------------------
void LinuxTcpConnection::tryRecv()
{
    const int MAX_BUFFER_SIZE = iseApp().iseOptions().getTcpMaxRecvBufferSize();
    if (recvTaskQueue_.empty() && recvBuffer_.getReadableBytes() >= MAX_BUFFER_SIZE)
    {
        setRecvEnabled(false);
        return;
    }

    const int BUFFER_SIZE = 1024*16;
    char dataBuf[BUFFER_SIZE];

    int bytesRecved = recvBuffer(dataBuf, BUFFER_SIZE, false);
    if (bytesRecved < 0)
    {
        errorOccurred();
        return;
    }

    if (bytesRecved > 0)
        recvBuffer_.append(dataBuf, bytesRecved);

    while (!recvTaskQueue_.empty())
    {
        bool packetRecved = tryRetrievePacket();
        if (!packetRecved)
            break;
    }
}

//-----------------------------------------------------------------------------
// ����: ���Դӻ�����ȡ��һ���������ݰ�
//-----------------------------------------------------------------------------
bool LinuxTcpConnection::tryRetrievePacket()
{
    if (recvTaskQueue_.empty()) return false;

    bool result = false;
    RecvTask& task = recvTaskQueue_.front();
    const char *buffer = recvBuffer_.peek();
    int readableBytes = recvBuffer_.getReadableBytes();

    if (readableBytes > 0)
    {
        int packetSize = 0;
        task.packetSplitter(buffer, readableBytes, packetSize);
        if (packetSize > 0)
        {
            iseApp().iseBusiness().onTcpRecvComplete(shared_from_this(),
                (void*)buffer, packetSize, task.context);
            recvTaskQueue_.pop_front();
            recvBuffer_.retrieve(packetSize);
            result = true;
        }
    }

    return result;
}

//-----------------------------------------------------------------------------
// ����: �� postRecvTask() �е��ô˺���
//-----------------------------------------------------------------------------
void LinuxTcpConnection::afterPostRecvTask(const TcpConnectionPtr& thisObj)
{
    LinuxTcpConnection *thisPtr = static_cast<LinuxTcpConnection*>(thisObj.get());
    if (!thisPtr->isErrorOccurred_)
        thisPtr->tryRetrievePacket();
}

///////////////////////////////////////////////////////////////////////////////
// class EpollObject

EpollObject::EpollObject()
{
    events_.resize(INITIAL_EVENT_SIZE);
    createEpoll();
    createPipe();
}

EpollObject::~EpollObject()
{
    destroyPipe();
    destroyEpoll();
}

//-----------------------------------------------------------------------------
// ����: ִ��һ�� EPoll ��ѭ
//-----------------------------------------------------------------------------
void EpollObject::poll()
{
    const int EPOLL_WAIT_TIMEOUT = 1000*1;  // ms

    int eventCount = ::epoll_wait(epollFd_, &events_[0], (int)events_.size(), EPOLL_WAIT_TIMEOUT);
    if (eventCount > 0)
    {
        processEvents(eventCount);

        if (eventCount == (int)events_.size())
            events_.resize(eventCount * 2);
    }
    else if (eventCount < 0)
    {
        logger().writeStr(SEM_EPOLL_WAIT_ERROR);
    }
}

//-----------------------------------------------------------------------------
// ����: �������������� Poll() ����
//-----------------------------------------------------------------------------
void EpollObject::wakeup()
{
    BYTE val = 0;
    ::write(pipeFds_[1], &val, sizeof(BYTE));
}

//-----------------------------------------------------------------------------
// ����: �� EPoll �����һ������
//-----------------------------------------------------------------------------
void EpollObject::addConnection(TcpConnection *connection, bool enableSend, bool enableRecv)
{
    epollControl(
        EPOLL_CTL_ADD, connection, connection->getSocket().getHandle(),
        enableSend, enableRecv);
}

//-----------------------------------------------------------------------------
// ����: ���� EPoll �е�һ������
//-----------------------------------------------------------------------------
void EpollObject::updateConnection(TcpConnection *connection, bool enableSend, bool enableRecv)
{
    epollControl(
        EPOLL_CTL_MOD, connection, connection->getSocket().getHandle(),
        enableSend, enableRecv);
}

//-----------------------------------------------------------------------------
// ����: �� EPoll ��ɾ��һ������
//-----------------------------------------------------------------------------
void EpollObject::removeConnection(TcpConnection *connection)
{
    epollControl(
        EPOLL_CTL_DEL, connection, connection->getSocket().getHandle(),
        false, false);
}

//-----------------------------------------------------------------------------
// ����: ���ûص�
//-----------------------------------------------------------------------------
void EpollObject::setNotifyEventCallback(const NotifyEventCallback& callback)
{
    onNotifyEvent_ = callback;
}

//-----------------------------------------------------------------------------

void EpollObject::createEpoll()
{
    epollFd_ = ::epoll_create(1024);
    if (epollFd_ < 0)
        logger().writeStr(SEM_CREATE_EPOLL_ERROR);
}

//-----------------------------------------------------------------------------

void EpollObject::destroyEpoll()
{
    if (epollFd_ > 0)
        ::close(epollFd_);
}

//-----------------------------------------------------------------------------

void EpollObject::createPipe()
{
    // pipeFds_[0] for reading, pipeFds_[1] for writing.
    memset(pipeFds_, 0, sizeof(pipeFds_));
    if (::pipe(pipeFds_) == 0)
        epollControl(EPOLL_CTL_ADD, NULL, pipeFds_[0], false, true);
    else
        logger().writeStr(SEM_CREATE_PIPE_ERROR);
}

//-----------------------------------------------------------------------------

void EpollObject::destroyPipe()
{
    epollControl(EPOLL_CTL_DEL, NULL, pipeFds_[0], false, false);

    if (pipeFds_[0]) close(pipeFds_[0]);
    if (pipeFds_[1]) close(pipeFds_[1]);

    memset(pipeFds_, 0, sizeof(pipeFds_));
}

//-----------------------------------------------------------------------------

void EpollObject::epollControl(int operation, void *param, int handle,
    bool enableSend, bool enableRecv)
{
    // ע: ���� Level Triggered (LT, Ҳ�� "��ƽ����") ģʽ

    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.data.ptr = param;
    if (enableSend)
        event.events |= EPOLLOUT;
    if (enableRecv)
        event.events |= (EPOLLIN | EPOLLPRI);

    if (::epoll_ctl(epollFd_, operation, handle, &event) < 0)
    {
        logger().writeFmt(SEM_EPOLL_CTRL_ERROR, operation);
    }
}

//-----------------------------------------------------------------------------
// ����: ����ܵ��¼�
//-----------------------------------------------------------------------------
void EpollObject::processPipeEvent()
{
    BYTE val;
    ::read(pipeFds_[0], &val, sizeof(BYTE));
}

//-----------------------------------------------------------------------------
// ����: ���� EPoll ��ѭ����¼�
//-----------------------------------------------------------------------------
void EpollObject::processEvents(int eventCount)
{
/*
// from epoll.h
enum EPOLL_EVENTS
{
    EPOLLIN      = 0x001,
    EPOLLPRI     = 0x002,
    EPOLLOUT     = 0x004,
    EPOLLRDNORM  = 0x040,
    EPOLLRDBAND  = 0x080,
    EPOLLWRNORM  = 0x100,
    EPOLLWRBAND  = 0x200,
    EPOLLMSG     = 0x400,
    EPOLLERR     = 0x008,
    EPOLLHUP     = 0x010,
    EPOLLRDHUP   = 0x2000,
    EPOLLONESHOT = (1 << 30),
    EPOLLET      = (1 << 31)
};
*/

#ifndef EPOLLRDHUP
#define EPOLLRDHUP 0x2000
#endif

    for (int i = 0; i < eventCount; i++)
    {
        epoll_event& ev = events_[i];
        if (ev.data.ptr == NULL)  // for pipe
        {
            processPipeEvent();
        }
        else
        {
            TcpConnection *connection = (TcpConnection*)ev.data.ptr;
            EVENT_TYPE eventType = ET_NONE;

            //logger().writeFmt("processEvents: %u", ev.events);  // debug

            if ((ev.events & EPOLLERR) || ((ev.events & EPOLLHUP) && !(ev.events & EPOLLIN)))
                eventType = ET_ERROR;
            else if (ev.events & (EPOLLIN | EPOLLPRI | EPOLLRDHUP))
                eventType = ET_ALLOW_RECV;
            else if (ev.events & EPOLLOUT)
                eventType = ET_ALLOW_SEND;

            if (eventType != ET_NONE && onNotifyEvent_)
                onNotifyEvent_(connection, eventType);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// class LinuxTcpEventLoop

LinuxTcpEventLoop::LinuxTcpEventLoop()
{
    epollObject_ = new EpollObject();
    epollObject_->setNotifyEventCallback(boost::bind(&LinuxTcpEventLoop::onEpollNotifyEvent, this, _1, _2));
}

LinuxTcpEventLoop::~LinuxTcpEventLoop()
{
    stop(false, true);
    delete epollObject_;
}

//-----------------------------------------------------------------------------
// ����: ���´� eventLoop �е�ָ�����ӵ�����
//-----------------------------------------------------------------------------
void LinuxTcpEventLoop::updateConnection(TcpConnection *connection, bool enableSend, bool enableRecv)
{
    epollObject_->updateConnection(connection, enableSend, enableRecv);
}

//-----------------------------------------------------------------------------
// ����: ִ�е����¼�ѭ���еĹ���
//-----------------------------------------------------------------------------
void LinuxTcpEventLoop::doLoopWork(Thread *thread)
{
    epollObject_->poll();
}

//-----------------------------------------------------------------------------
// ����: �����¼�ѭ���е���������
//-----------------------------------------------------------------------------
void LinuxTcpEventLoop::wakeupLoop()
{
    epollObject_->wakeup();
}

//-----------------------------------------------------------------------------
// ����: ��������ע�ᵽ�¼�ѭ����
//-----------------------------------------------------------------------------
void LinuxTcpEventLoop::registerConnection(TcpConnection *connection)
{
    epollObject_->addConnection(connection, false, false);
}

//-----------------------------------------------------------------------------
// ����: ���¼�ѭ����ע������
//-----------------------------------------------------------------------------
void LinuxTcpEventLoop::unregisterConnection(TcpConnection *connection)
{
    epollObject_->removeConnection(connection);
}

//-----------------------------------------------------------------------------
// ����: EPoll �¼��ص�
//-----------------------------------------------------------------------------
void LinuxTcpEventLoop::onEpollNotifyEvent(TcpConnection *connection, EpollObject::EVENT_TYPE eventType)
{
    LinuxTcpConnection *conn = static_cast<LinuxTcpConnection*>(connection);

    if (eventType == EpollObject::ET_ALLOW_SEND)
        conn->trySend();
    else if (eventType == EpollObject::ET_ALLOW_RECV)
        conn->tryRecv();
    else if (eventType == EpollObject::ET_ERROR)
        conn->errorOccurred();
}

///////////////////////////////////////////////////////////////////////////////

#endif  /* ifdef ISE_LINUX */

///////////////////////////////////////////////////////////////////////////////
// class MainTcpServer

MainTcpServer::MainTcpServer() :
    isActive_(false)
{
    createTcpServerList();
}

MainTcpServer::~MainTcpServer()
{
    close();
    destroyTcpServerList();
}

//-----------------------------------------------------------------------------
// ����: ����������
//-----------------------------------------------------------------------------
void MainTcpServer::open()
{
    if (!isActive_)
    {
        try
        {
            doOpen();
            isActive_ = true;
        }
        catch (...)
        {
            doClose();
            throw;
        }
    }
}

//-----------------------------------------------------------------------------
// ����: �رշ�����
//-----------------------------------------------------------------------------
void MainTcpServer::close()
{
    if (isActive_)
    {
        doClose();
        isActive_ = false;
    }
}

//-----------------------------------------------------------------------------

TcpServer& MainTcpServer::getTcpServer(int index)
{
    ISE_ASSERT(index >= 0 && index < (int)tcpServerList_.size());
    return *tcpServerList_[index];
}

//-----------------------------------------------------------------------------
// ����: �ӳٴ��� tcpClientEventLoopList_
//-----------------------------------------------------------------------------
TcpEventLoopList& MainTcpServer::getTcpClientEventLoopList()
{
    static Mutex s_mutex;
    AutoLocker locker(s_mutex);

    if (!tcpClientEventLoopList_)
    {
        int eventLoopCount = iseApp().iseOptions().getTcpClientEventLoopCount();
        tcpClientEventLoopList_.reset(new TcpEventLoopList(eventLoopCount));
        if (isActive_)
            tcpClientEventLoopList_->start();
    }

    return *tcpClientEventLoopList_;
}

//-----------------------------------------------------------------------------
// ����: ����TCP������
//-----------------------------------------------------------------------------
void MainTcpServer::createTcpServerList()
{
    int serverCount = iseApp().iseOptions().getTcpServerCount();
    ISE_ASSERT(serverCount >= 0);

        tcpServerList_.resize(serverCount);
        for (int i = 0; i < serverCount; i++)
        {
            TcpServer *tcpServer = new TcpServer(iseApp().iseOptions().getTcpServerEventLoopCount(i));
            tcpServer->setContext(i);
            tcpServer->setLocalPort(static_cast<WORD>(iseApp().iseOptions().getTcpServerPort(i)));
            tcpServer->setLocalAddr(iseApp().iseOptions().getTcpServerAddr(i));

        tcpServerList_[i] = tcpServer;
    }
}

//-----------------------------------------------------------------------------
// ����: ����TCP������
//-----------------------------------------------------------------------------
void MainTcpServer::destroyTcpServerList()
{
    for (int i = 0; i < (int)tcpServerList_.size(); i++)
        delete tcpServerList_[i];
    tcpServerList_.clear();
}

//-----------------------------------------------------------------------------
// ����: ����������
//-----------------------------------------------------------------------------
void MainTcpServer::doOpen()
{
    for (int i = 0; i < (int)tcpServerList_.size(); i++)
        tcpServerList_[i]->open();

    if (tcpClientEventLoopList_)
        tcpClientEventLoopList_->start();
}

//-----------------------------------------------------------------------------
// ����: �رշ�����
//-----------------------------------------------------------------------------
void MainTcpServer::doClose()
{
    if (tcpClientEventLoopList_)
        tcpClientEventLoopList_->stop();

    for (int i = 0; i < (int)tcpServerList_.size(); i++)
        tcpServerList_[i]->close();
}

///////////////////////////////////////////////////////////////////////////////

} // namespace ise
