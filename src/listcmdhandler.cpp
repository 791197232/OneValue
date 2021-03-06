﻿#include "util/locker.h"
#include "redisproxy.h"
#include "t_list.h"
#include "listcmdhandler.h"

class ListMutex
{
public:
    enum { Size = 128 };
    ListMutex(void) {}
    ~ListMutex(void) {}

    void lock(const std::string& name) {
        unsigned int index = hashForBytes(name.c_str(), name.length()) % Size;
        m_mutex[index].lock();
    }

    void unlock(const std::string& name) {
        unsigned int index = hashForBytes(name.c_str(), name.length()) % Size;
        m_mutex[index].unlock();
    }

private:
    Mutex m_mutex[Size];
};

static ListMutex list_mutex;

void onLindexCommand(ClientPacket* packet, void*)
{
    RedisProtoParseResult& r = packet->recvParseResult;
    if (r.tokenCount != 3) {
        packet->setFinishedState(ClientPacket::WrongNumberOfArguments);
        return;
    }

    std::string name(r.tokens[1].s, r.tokens[1].len);
    TList list(packet->proxy()->leveldbCluster(), name);
    std::string indexString(r.tokens[2].s, r.tokens[2].len);
    IOBuffer& reply = packet->sendBuff;
    if (!TRedisHelper::isInteger(indexString)) {
        reply.appendFormatString("-ERR value is not an integer or out of range\r\n");
        packet->setFinishedState(ClientPacket::RequestFinished);
        return;
    }
    int index = atoi(indexString.c_str());
    std::string value;

    list_mutex.lock(name);
    bool ok = list.lindex(index, &value);
    list_mutex.unlock(name);

    if (!ok) {
        reply.append("$-1\r\n");
        packet->setFinishedState(ClientPacket::RequestFinished);
        return;
    }

    reply.appendFormatString("$%d\r\n", value.size());
    reply.append(value.data(), value.size());
    reply.append("\r\n");
    packet->setFinishedState(ClientPacket::RequestFinished);
}


void onLlenCommand(ClientPacket* packet, void*)
{
    RedisProtoParseResult& r = packet->recvParseResult;
    if (r.tokenCount != 2) {
        packet->setFinishedState(ClientPacket::WrongNumberOfArguments);
        return;
    }

    std::string name(r.tokens[1].s, r.tokens[1].len);
    TList list(packet->proxy()->leveldbCluster(), name);

    list_mutex.lock(name);
    int len = list.llen();
    list_mutex.unlock(name);

    packet->sendBuff.appendFormatString(":%d\r\n", len);
    packet->setFinishedState(ClientPacket::RequestFinished);
}


void onLinsertCommand(ClientPacket* packet, void*)
{
    RedisProtoParseResult& r = packet->recvParseResult;
    if (r.tokenCount != 5) {
        packet->setFinishedState(ClientPacket::WrongNumberOfArguments);
        return;
    }

    std::string name(r.tokens[1].s, r.tokens[1].len);
    TList list(packet->proxy()->leveldbCluster(), name);
    const std::string how(r.tokens[2].s, r.tokens[2].len);
    const std::string existValue(r.tokens[3].s, r.tokens[3].len);
    const std::string value(r.tokens[4].s, r.tokens[4].len);
    stringlist result;

    list_mutex.lock(name);
    bool ok = list.lrange(0, -1, &result);
    list_mutex.unlock(name);

    if (!ok) {
        packet->sendBuff.append(":0\r\n");
        packet->setFinishedState(ClientPacket::RequestFinished);
        return;
    }
    stringlist::const_iterator itList = result.begin();
    int where = 0;
    bool find = false;
    for (; itList != result.end(); ++itList) {
        if (existValue == *itList) {
            find = true;
            break;
        }
        ++where;
    }
    if (!find) {
        // doesn't exist this value
        packet->sendBuff.append(":-1\r\n");
        packet->setFinishedState(ClientPacket::RequestFinished);
        return;
    }
    if (0 == strcasecmp("after", how.c_str())) {
        list_mutex.lock(name);
        ok = list.linsert(where + 1, value);
        list_mutex.unlock(name);
        if (!ok) {
            packet->sendBuff.append(":-1\r\n");
            packet->setFinishedState(ClientPacket::RequestFinished);
            return;
        }
        packet->sendBuff.appendFormatString(":%d\r\n", result.size() + 1);
        packet->setFinishedState(ClientPacket::RequestFinished);
        return;
    }
    if (0 == strcasecmp("before", how.c_str())) {
        list_mutex.lock(name);
        ok = list.linsert(where, value);
        list_mutex.unlock(name);
        if (!ok) {
            packet->sendBuff.append(":-1\r\n");
            packet->setFinishedState(ClientPacket::RequestFinished);
            return;
        }
        packet->sendBuff.appendFormatString(":%d\r\n", result.size() + 1);
        packet->setFinishedState(ClientPacket::RequestFinished);
        return;
    }
}


void onLpopCommand(ClientPacket* packet, void*)
{
    RedisProtoParseResult& r = packet->recvParseResult;
    if (r.tokenCount != 2) {
        packet->setFinishedState(ClientPacket::WrongNumberOfArguments);
        return;
    }

    std::string name(r.tokens[1].s, r.tokens[1].len);
    TList list(packet->proxy()->leveldbCluster(), name);
    std::string value;
    IOBuffer& reply = packet->sendBuff;

    list_mutex.lock(name);
    bool ok = list.lpop(&value);
    list_mutex.unlock(name);

    if (!ok) {
        packet->sendBuff.append("$-1\r\n");
        packet->setFinishedState(ClientPacket::RequestFinished);
        return;
    }
    reply.appendFormatString("$%d\r\n", value.size());
    reply.append(value.data(), value.size());
    reply.append("\r\n");
    packet->setFinishedState(ClientPacket::RequestFinished);
}


void onLpushCommand(ClientPacket* packet, void*)
{
    RedisProtoParseResult& r = packet->recvParseResult;
    int tokenCnt = r.tokenCount;
    if (tokenCnt < 3) {
        packet->setFinishedState(ClientPacket::WrongNumberOfArguments);
        return;
    }

    std::string name(r.tokens[1].s, r.tokens[1].len);
    TList list(packet->proxy()->leveldbCluster(), name);
    int size = 0;
    for (int i = 2; i < tokenCnt; ++i) {
        std::string val = std::string(r.tokens[i].s, r.tokens[i].len);

        list_mutex.lock(name);
        size = list.lpush(val);
        list_mutex.unlock(name);
    }
    packet->sendBuff.appendFormatString(":%d\r\n", size);
    packet->setFinishedState(ClientPacket::RequestFinished);
}


void onLpushxCommand(ClientPacket* packet, void*)
{
    RedisProtoParseResult& r = packet->recvParseResult;
    int tokenCnt = r.tokenCount;
    if (tokenCnt != 3) {
        packet->setFinishedState(ClientPacket::WrongNumberOfArguments);
        return;
    }

    std::string name(r.tokens[1].s, r.tokens[1].len);
    TList list(packet->proxy()->leveldbCluster(), name);
    int size = 0;
    std::string val = std::string(r.tokens[2].s, r.tokens[2].len);

    list_mutex.lock(name);
    size = list.lpushx(val);
    list_mutex.unlock(name);

    packet->sendBuff.appendFormatString(":%d\r\n", size);
    packet->setFinishedState(ClientPacket::RequestFinished);
}


void onLRangeCommand(ClientPacket *packet, void*)
{
    RedisProtoParseResult& r = packet->recvParseResult;
    if (r.tokenCount != 4) {
        packet->setFinishedState(ClientPacket::WrongNumberOfArguments);
        return;
    }

    std::string name(r.tokens[1].s, r.tokens[1].len);
    TList list(packet->proxy()->leveldbCluster(), name);
    int start = atoi(std::string(r.tokens[2].s, r.tokens[2].len).c_str());
    int stop = atoi(std::string(r.tokens[3].s, r.tokens[3].len).c_str());
    stringlist result;

    list_mutex.lock(name);
    list.lrange(start, stop, &result);
    list_mutex.unlock(name);

    packet->sendBuff.appendFormatString("*%d\r\n", result.size());
    for (stringlist::iterator it = result.begin(); it != result.end(); ++it) {
        std::string& s = (*it);
        packet->sendBuff.appendFormatString("$%d\r\n", s.length());
        packet->sendBuff.append(s.data(), s.size());
        packet->sendBuff.append("\r\n", 2);
    }

    packet->setFinishedState(ClientPacket::RequestFinished);
}

void onLRemCommand(ClientPacket *packet, void *)
{
    RedisProtoParseResult& r = packet->recvParseResult;
    if (r.tokenCount != 4) {
        packet->setFinishedState(ClientPacket::WrongNumberOfArguments);
        return;
    }

    std::string str_count(r.tokens[2].s, r.tokens[2].len);
    if (!TRedisHelper::isInteger(str_count)) {
        packet->sendBuff.append("-ERR value is not an integer or out of range\r\n");
        packet->setFinishedState(ClientPacket::RequestFinished);
        return;
    }

    std::string name(r.tokens[1].s, r.tokens[1].len);
    int removeCount = atoi(str_count.c_str());
    TList list(packet->proxy()->leveldbCluster(), name);

    list_mutex.lock(name);
    int ret = list.lrem(removeCount, std::string(r.tokens[3].s, r.tokens[3].len));
    list_mutex.unlock(name);

    packet->sendBuff.appendFormatString(":%d\r\n", ret);
    packet->setFinishedState(ClientPacket::RequestFinished);
}


void onLSetCommand(ClientPacket *packet, void *)
{
    RedisProtoParseResult& r = packet->recvParseResult;
    if (r.tokenCount != 4) {
        packet->setFinishedState(ClientPacket::WrongNumberOfArguments);
        return;
    }

    std::string str_index(r.tokens[2].s, r.tokens[2].len);
    if (!TRedisHelper::isInteger(str_index)) {
        packet->sendBuff.append("-ERR value is not an integer or out of range\r\n");
        packet->setFinishedState(ClientPacket::RequestFinished);
        return;
    }

    int index = atoi(str_index.c_str());
    std::string name(r.tokens[1].s, r.tokens[1].len);
    TList list(packet->proxy()->leveldbCluster(), name);

    list_mutex.lock(name);
    bool ok = list.lset(index, std::string(r.tokens[3].s, r.tokens[3].len));
    list_mutex.unlock(name);

    if (!ok) {
        packet->sendBuff.appendFormatString("-ERR %s\r\n", list.lastError().c_str());
    } else {
        packet->sendBuff.append("+OK\r\n");
    }

    packet->setFinishedState(ClientPacket::RequestFinished);
}


void onLTrimCommand(ClientPacket *packet, void *)
{
    RedisProtoParseResult& r = packet->recvParseResult;
    if (r.tokenCount != 4) {
        packet->setFinishedState(ClientPacket::WrongNumberOfArguments);
        return;
    }

    std::string str_start(r.tokens[2].s, r.tokens[2].len);
    std::string str_stop(r.tokens[3].s, r.tokens[3].len);
    if (!TRedisHelper::isInteger(str_start) || !TRedisHelper::isInteger(str_stop)) {
        packet->sendBuff.append("-ERR value is not an integer or out of range\r\n");
        packet->setFinishedState(ClientPacket::RequestFinished);
        return;
    }

    int start = atoi(str_start.c_str());
    int stop = atoi(str_stop.c_str());
    std::string name(r.tokens[1].s, r.tokens[1].len);

    TList list(packet->proxy()->leveldbCluster(), name);
    list_mutex.lock(name);
    list.ltrim(start, stop);
    list_mutex.unlock(name);

    packet->sendBuff.append("+OK\r\n");
    packet->setFinishedState(ClientPacket::RequestFinished);
}

void onRPopCommand(ClientPacket *packet, void *)
{
    RedisProtoParseResult& r = packet->recvParseResult;
    if (r.tokenCount != 2) {
        packet->setFinishedState(ClientPacket::WrongNumberOfArguments);
        return;
    }

    std::string name(r.tokens[1].s, r.tokens[1].len);
    TList list(packet->proxy()->leveldbCluster(), name);
    std::string value;

    list_mutex.lock(name);
    bool ok = list.rpop(&value);
    list_mutex.unlock(name);

    if (ok) {
        packet->sendBuff.appendFormatString("$%d\r\n", value.size());
        packet->sendBuff.append(value.data(), value.size());
        packet->sendBuff.append("\r\n", 2);
    } else {
        packet->sendBuff.append("$-1\r\n", 5);
    }
    packet->setFinishedState(ClientPacket::RequestFinished);
}

void onRPopLPushCommand(ClientPacket* packet, void*)
{
    RedisProtoParseResult& r = packet->recvParseResult;
    if (r.tokenCount != 3) {
        packet->setFinishedState(ClientPacket::WrongNumberOfArguments);
        return;
    }

    std::string srcname(r.tokens[1].s, r.tokens[1].len);
    std::string destname(r.tokens[2].s, r.tokens[2].len);

    TList src(packet->proxy()->leveldbCluster(), srcname);
    TList dest(packet->proxy()->leveldbCluster(), destname);

    std::string popvalue;

    list_mutex.lock(srcname);
    bool ok = src.rpop(&popvalue);
    list_mutex.unlock(srcname);
    if (!ok) {
        packet->sendBuff.append("$-1\r\n");
        packet->setFinishedState(ClientPacket::RequestFinished);
        return;
    }

    list_mutex.lock(destname);
    dest.lpush(popvalue);
    list_mutex.unlock(destname);

    packet->sendBuff.appendFormatString("$%d\r\n", popvalue.size());
    packet->sendBuff.append(popvalue.data(), popvalue.size());
    packet->sendBuff.append("\r\n");
    packet->setFinishedState(ClientPacket::RequestFinished);
}

void onRPushCommand(ClientPacket *packet, void*)
{
    RedisProtoParseResult& r = packet->recvParseResult;
    if (r.tokenCount < 3) {
        packet->setFinishedState(ClientPacket::WrongNumberOfArguments);
        return;
    }

    int count = 0;
    std::string name(r.tokens[1].s, r.tokens[1].len);
    TList list(packet->proxy()->leveldbCluster(), name);
    for (int i = 2; i < r.tokenCount; ++i) {
        std::string value(r.tokens[i].s, r.tokens[i].len);
        list_mutex.lock(name);
        count = list.rpush(value);
        list_mutex.unlock(name);
    }
    packet->sendBuff.appendFormatString(":%d\r\n", count);
    packet->setFinishedState(ClientPacket::RequestFinished);
}

void onRpushxCommand(ClientPacket *packet, void *)
{
    RedisProtoParseResult& r = packet->recvParseResult;
    if (r.tokenCount != 3) {
        packet->setFinishedState(ClientPacket::WrongNumberOfArguments);
        return;
    }

    std::string name(r.tokens[1].s, r.tokens[1].len);
    TList list(packet->proxy()->leveldbCluster(), name);
    std::string value(r.tokens[2].s, r.tokens[2].len);

    list_mutex.lock(name);
    int count = list.rpushx(value);
    list_mutex.unlock(name);

    packet->sendBuff.appendFormatString(":%d\r\n", count);
    packet->setFinishedState(ClientPacket::RequestFinished);
}


void onLClearCommand(ClientPacket *packet, void *)
{
    RedisProtoParseResult& r = packet->recvParseResult;
    if (r.tokenCount != 2) {
        packet->setFinishedState(ClientPacket::WrongNumberOfArguments);
        return;
    }

    std::string name(r.tokens[1].s, r.tokens[1].len);
    TList list(packet->proxy()->leveldbCluster(), name);

    list_mutex.lock(name);
    list.lclear();
    list_mutex.unlock(name);

    packet->sendBuff.append("+OK\r\n");
    packet->setFinishedState(ClientPacket::RequestFinished);
}
