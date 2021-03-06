﻿#include <time.h>

#include "util/logger.h"
#include "t_zset.h"
#include "t_hash.h"
#include "ttlmanager.h"

#include "dbcopy.h"

DBCopy::DBCopy(void) : m_maxPipeline(32)
{
}

DBCopy::~DBCopy(void)
{
}

void DBCopy::setMaxPipeline(int n)
{
    if (n > 0) {
        m_maxPipeline = n;
    }
}

bool DBCopy::start(LeveldbCluster* src, const HostAddress& dest)
{
    TcpSocket socket = TcpSocket::createTcpSocket();
    if (!socket.connect(dest)) {
        socket.close();
        return false;
    }

    for (int i = 0; i < src->databaseCount(); ++i) {
        Leveldb* db = src->database(i);
        LeveldbIterator iter;
        if (!db->initIterator(iter)) {
            continue;
        }

        IOBuffer sendbuff;
        int records = 0;
        iter.seekToFirst();
        while (iter.isValid()) {
            XObject key = iter.key();
            XObject value = iter.value();

            if ((records % m_maxPipeline) == 0) {
                if (!sendbuff.isEmpty()) {
                    int sendBytes = socket.send(sendbuff.data(), sendbuff.size());
                    if (sendBytes <= 0) {
                        socket.close();
                        return false;
                    }
                }
                sendbuff.clear();
            }
            makeCommand(&sendbuff, key, value);
            iter.next();
            ++records;
        }

        if (!sendbuff.isEmpty()) {
            int sendBytes = socket.send(sendbuff.data(), sendbuff.size());
            if (sendBytes <= 0) {
                socket.close();
                return false;
            }
        }
    }
    return true;
}

void DBCopy::makeCommand(IOBuffer* buff, const XObject &key, const XObject &value)
{
    short type = *((short*)key.data);
    switch (type) {
    case T_KV:
    case T_List:
    case T_ListElement:
    case T_Ttl: {
        buff->append("*3\r\n$6\r\nRAWSET\r\n", 16);
        buff->appendFormatString("$%d\r\n", key.len);
        buff->append(key.data, key.len);
        buff->append("\r\n", 2);
        buff->appendFormatString("$%d\r\n", value.len);
        buff->append(value.data, value.len);
        buff->append("\r\n", 2);
    }
        break;
    case T_Set:
    case T_ZSet:
    case T_Hash: {
        HashKeyInfo hashkey;
        THash::unmakeHashKey(key.data, key.len, &hashkey);
        buff->append("*4\r\n$6\r\nRAWSET\r\n", 16);
        buff->appendFormatString("$%d\r\n", key.len);
        buff->append(key.data, key.len);
        buff->append("\r\n", 2);
        buff->appendFormatString("$%d\r\n", value.len);
        buff->append(value.data, value.len);
        buff->append("\r\n", 2);
        buff->appendFormatString("$%d\r\n", hashkey.name.len);
        buff->append(hashkey.name.data, hashkey.name.len);
        buff->append("\r\n", 2);
    }
        break;
    default:
        break;
    }
}
