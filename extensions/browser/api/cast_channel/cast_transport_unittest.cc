// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/api/cast_channel/cast_transport.h"

#include <stddef.h>
#include <queue>

#include "base/test/simple_test_tick_clock.h"
#include "extensions/browser/api/cast_channel/cast_framer.h"
#include "extensions/browser/api/cast_channel/cast_socket.h"
#include "extensions/browser/api/cast_channel/logger.h"
#include "extensions/browser/api/cast_channel/logger_util.h"
#include "extensions/browser/api/cast_channel/test_util.h"
#include "extensions/common/api/cast_channel/cast_channel.pb.h"
#include "net/base/capturing_net_log.h"
#include "net/base/completion_callback.h"
#include "net/base/net_errors.h"
#include "net/socket/socket.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::DoAll;
using testing::InSequence;
using testing::Invoke;
using testing::NotNull;
using testing::Return;
using testing::WithArg;

namespace extensions {
namespace core_api {
namespace cast_channel {
namespace {
// Mockable placeholder for write completion events.
class CompleteHandler {
 public:
  CompleteHandler() {}
  MOCK_METHOD1(Complete, void(int result));

 private:
  DISALLOW_COPY_AND_ASSIGN(CompleteHandler);
};

// Creates a CastMessage proto with the bare minimum required fields set.
CastMessage CreateCastMessage() {
  CastMessage output;
  output.set_protocol_version(CastMessage::CASTV2_1_0);
  output.set_namespace_("x");
  output.set_source_id("source");
  output.set_destination_id("destination");
  output.set_payload_type(CastMessage::STRING);
  output.set_payload_utf8("payload");
  return output;
}

// FIFO queue of completion callbacks. Outstanding write operations are
// Push()ed into the queue. Callback completion is simulated by invoking
// Pop() in the same order as Push().
class CompletionQueue {
 public:
  CompletionQueue() {}
  ~CompletionQueue() { CHECK_EQ(0u, cb_queue_.size()); }

  // Enqueues a pending completion callback.
  void Push(const net::CompletionCallback& cb) { cb_queue_.push(cb); }
  // Runs the next callback and removes it from the queue.
  void Pop(int rv) {
    CHECK_GT(cb_queue_.size(), 0u);
    cb_queue_.front().Run(rv);
    cb_queue_.pop();
  }

 private:
  std::queue<net::CompletionCallback> cb_queue_;
  DISALLOW_COPY_AND_ASSIGN(CompletionQueue);
};

// GMock action that reads data from an IOBuffer and writes it to a string
// variable.
//
//   buf_idx (template parameter 0): 0-based index of the net::IOBuffer
//                                   in the function mock arg list.
//   size_idx (template parameter 1): 0-based index of the byte count arg.
//   str: pointer to the string which will receive data from the buffer.
ACTION_TEMPLATE(ReadBufferToString,
                HAS_2_TEMPLATE_PARAMS(int, buf_idx, int, size_idx),
                AND_1_VALUE_PARAMS(str)) {
  str->assign(testing::get<buf_idx>(args)->data(),
              testing::get<size_idx>(args));
}

// GMock action that writes data from a string to an IOBuffer.
//
//   buf_idx (template parameter 0): 0-based index of the IOBuffer arg.
//   str: the string containing data to be written to the IOBuffer.
ACTION_TEMPLATE(FillBufferFromString,
                HAS_1_TEMPLATE_PARAMS(int, buf_idx),
                AND_1_VALUE_PARAMS(str)) {
  memcpy(testing::get<buf_idx>(args)->data(), str.data(), str.size());
}

// GMock action that enqueues a write completion callback in a queue.
//
//   buf_idx (template parameter 0): 0-based index of the CompletionCallback.
//   completion_queue: a pointer to the CompletionQueue.
ACTION_TEMPLATE(EnqueueCallback,
                HAS_1_TEMPLATE_PARAMS(int, cb_idx),
                AND_1_VALUE_PARAMS(completion_queue)) {
  completion_queue->Push(testing::get<cb_idx>(args));
}

// Checks if two proto messages are the same.
// From
// third_party/cacheinvalidation/overrides/google/cacheinvalidation/deps/gmock.h
MATCHER_P(EqualsProto, message, "") {
  std::string expected_serialized, actual_serialized;
  message.SerializeToString(&expected_serialized);
  arg.SerializeToString(&actual_serialized);
  return expected_serialized == actual_serialized;
}
}  // namespace

class MockCastTransportDelegate : public CastTransport::Delegate {
 public:
  MOCK_METHOD2(OnError,
               void(ChannelError error, const LastErrors& last_errors));
  MOCK_METHOD1(OnMessage, void(const CastMessage& message));
};

class MockSocket : public net::Socket {
 public:
  MOCK_METHOD3(Read,
               int(net::IOBuffer* buf,
                   int buf_len,
                   const net::CompletionCallback& callback));

  MOCK_METHOD3(Write,
               int(net::IOBuffer* buf,
                   int buf_len,
                   const net::CompletionCallback& callback));

  virtual int SetReceiveBufferSize(int32 size) {
    NOTREACHED();
    return 0;
  }

  virtual int SetSendBufferSize(int32 size) {
    NOTREACHED();
    return 0;
  }
};

class CastTransportTest : public testing::Test {
 public:
  CastTransportTest()
      : logger_(new Logger(
            scoped_ptr<base::TickClock>(new base::SimpleTestTickClock),
            base::TimeTicks())) {
    transport_.reset(new CastTransportImpl(&mock_socket_, &delegate_, 0,
                                           CreateIPEndPointForTest(),
                                           auth_type_, logger_));
  }
  ~CastTransportTest() override {}

 protected:
  MockCastTransportDelegate delegate_;
  MockSocket mock_socket_;
  ChannelAuthType auth_type_;
  Logger* logger_;
  scoped_ptr<CastTransport> transport_;
};

// ----------------------------------------------------------------------------
// Asynchronous write tests
TEST_F(CastTransportTest, TestFullWriteAsync) {
  CompletionQueue socket_cbs;
  CompleteHandler write_handler;
  std::string output;

  CastMessage message = CreateCastMessage();
  std::string serialized_message;
  EXPECT_TRUE(MessageFramer::Serialize(message, &serialized_message));

  EXPECT_CALL(mock_socket_, Write(NotNull(), serialized_message.size(), _))
      .WillOnce(DoAll(ReadBufferToString<0, 1>(&output),
                      EnqueueCallback<2>(&socket_cbs),
                      Return(net::ERR_IO_PENDING)));
  EXPECT_CALL(write_handler, Complete(net::OK));
  transport_->SendMessage(
      message,
      base::Bind(&CompleteHandler::Complete, base::Unretained(&write_handler)));
  socket_cbs.Pop(serialized_message.size());
  EXPECT_EQ(serialized_message, output);
}

TEST_F(CastTransportTest, TestPartialWritesAsync) {
  InSequence seq;
  CompletionQueue socket_cbs;
  CompleteHandler write_handler;
  std::string output;

  CastMessage message = CreateCastMessage();
  std::string serialized_message;
  EXPECT_TRUE(MessageFramer::Serialize(message, &serialized_message));

  // Only one byte is written.
  EXPECT_CALL(mock_socket_,
              Write(NotNull(), static_cast<int>(serialized_message.size()), _))
      .WillOnce(DoAll(ReadBufferToString<0, 1>(&output),
                      EnqueueCallback<2>(&socket_cbs),
                      Return(net::ERR_IO_PENDING)));
  // Remainder of bytes are written.
  EXPECT_CALL(
      mock_socket_,
      Write(NotNull(), static_cast<int>(serialized_message.size() - 1), _))
      .WillOnce(DoAll(ReadBufferToString<0, 1>(&output),
                      EnqueueCallback<2>(&socket_cbs),
                      Return(net::ERR_IO_PENDING)));

  transport_->SendMessage(
      message,
      base::Bind(&CompleteHandler::Complete, base::Unretained(&write_handler)));
  EXPECT_EQ(serialized_message, output);
  socket_cbs.Pop(1);

  EXPECT_CALL(write_handler, Complete(net::OK));
  socket_cbs.Pop(serialized_message.size() - 1);
  EXPECT_EQ(serialized_message.substr(1, serialized_message.size() - 1),
            output);
}

TEST_F(CastTransportTest, TestWriteFailureAsync) {
  CompletionQueue socket_cbs;
  CompleteHandler write_handler;
  CastMessage message = CreateCastMessage();
  EXPECT_CALL(mock_socket_, Write(NotNull(), _, _)).WillOnce(
      DoAll(EnqueueCallback<2>(&socket_cbs), Return(net::ERR_IO_PENDING)));
  EXPECT_CALL(write_handler, Complete(net::ERR_FAILED));
  transport_->SendMessage(
      message,
      base::Bind(&CompleteHandler::Complete, base::Unretained(&write_handler)));
  socket_cbs.Pop(net::ERR_CONNECTION_RESET);
}

// ----------------------------------------------------------------------------
// Synchronous write tests
TEST_F(CastTransportTest, TestFullWriteSync) {
  CompleteHandler write_handler;
  std::string output;
  CastMessage message = CreateCastMessage();
  std::string serialized_message;
  EXPECT_TRUE(MessageFramer::Serialize(message, &serialized_message));
  EXPECT_CALL(mock_socket_, Write(NotNull(), serialized_message.size(), _))
      .WillOnce(DoAll(ReadBufferToString<0, 1>(&output),
                      Return(serialized_message.size())));
  EXPECT_CALL(write_handler, Complete(net::OK));
  transport_->SendMessage(
      message,
      base::Bind(&CompleteHandler::Complete, base::Unretained(&write_handler)));
  EXPECT_EQ(serialized_message, output);
}

TEST_F(CastTransportTest, TestPartialWritesSync) {
  InSequence seq;
  CompleteHandler write_handler;
  std::string output;

  CastMessage message = CreateCastMessage();
  std::string serialized_message;
  EXPECT_TRUE(MessageFramer::Serialize(message, &serialized_message));

  // Only one byte is written.
  EXPECT_CALL(mock_socket_, Write(NotNull(), serialized_message.size(), _))
      .WillOnce(DoAll(ReadBufferToString<0, 1>(&output), Return(1)));
  // Remainder of bytes are written.
  EXPECT_CALL(mock_socket_, Write(NotNull(), serialized_message.size() - 1, _))
      .WillOnce(DoAll(ReadBufferToString<0, 1>(&output),
                      Return(serialized_message.size() - 1)));

  EXPECT_CALL(write_handler, Complete(net::OK));
  transport_->SendMessage(
      message,
      base::Bind(&CompleteHandler::Complete, base::Unretained(&write_handler)));
  EXPECT_EQ(serialized_message.substr(1, serialized_message.size() - 1),
            output);
}

TEST_F(CastTransportTest, TestWriteFailureSync) {
  CompleteHandler write_handler;
  CastMessage message = CreateCastMessage();
  EXPECT_CALL(mock_socket_, Write(NotNull(), _, _))
      .WillOnce(Return(net::ERR_CONNECTION_RESET));
  EXPECT_CALL(write_handler, Complete(net::ERR_FAILED));
  transport_->SendMessage(
      message,
      base::Bind(&CompleteHandler::Complete, base::Unretained(&write_handler)));
}

// ----------------------------------------------------------------------------
// Asynchronous read tests
TEST_F(CastTransportTest, TestFullReadAsync) {
  InSequence s;
  CompletionQueue socket_cbs;

  CastMessage message = CreateCastMessage();
  std::string serialized_message;
  EXPECT_TRUE(MessageFramer::Serialize(message, &serialized_message));

  // Read bytes [0, 3].
  EXPECT_CALL(mock_socket_,
              Read(NotNull(), MessageFramer::MessageHeader::header_size(), _))
      .WillOnce(DoAll(FillBufferFromString<0>(serialized_message),
                      EnqueueCallback<2>(&socket_cbs),
                      Return(net::ERR_IO_PENDING)));

  // Read bytes [4, n].
  EXPECT_CALL(mock_socket_,
              Read(NotNull(),
                   serialized_message.size() -
                       MessageFramer::MessageHeader::header_size(),
                   _))
      .WillOnce(DoAll(FillBufferFromString<0>(serialized_message.substr(
                          MessageFramer::MessageHeader::header_size(),
                          serialized_message.size() -
                              MessageFramer::MessageHeader::header_size())),
                      EnqueueCallback<2>(&socket_cbs),
                      Return(net::ERR_IO_PENDING)))
      .RetiresOnSaturation();

  EXPECT_CALL(delegate_, OnMessage(EqualsProto(message)));
  EXPECT_CALL(mock_socket_,
              Read(NotNull(), MessageFramer::MessageHeader::header_size(), _))
      .WillOnce(Return(net::ERR_IO_PENDING));
  transport_->StartReading();
  socket_cbs.Pop(MessageFramer::MessageHeader::header_size());
  socket_cbs.Pop(serialized_message.size() -
                 MessageFramer::MessageHeader::header_size());
}

TEST_F(CastTransportTest, TestPartialReadAsync) {
  InSequence s;
  CompletionQueue socket_cbs;

  CastMessage message = CreateCastMessage();
  std::string serialized_message;
  EXPECT_TRUE(MessageFramer::Serialize(message, &serialized_message));

  // Read bytes [0, 3].
  EXPECT_CALL(mock_socket_,
              Read(NotNull(), MessageFramer::MessageHeader::header_size(), _))
      .WillOnce(DoAll(FillBufferFromString<0>(serialized_message),
                      EnqueueCallback<2>(&socket_cbs),
                      Return(net::ERR_IO_PENDING)))
      .RetiresOnSaturation();
  // Read bytes [4, n-1].
  EXPECT_CALL(mock_socket_,
              Read(NotNull(),
                   serialized_message.size() -
                       MessageFramer::MessageHeader::header_size(),
                   _))
      .WillOnce(DoAll(FillBufferFromString<0>(serialized_message.substr(
                          MessageFramer::MessageHeader::header_size(),
                          serialized_message.size() -
                              MessageFramer::MessageHeader::header_size() - 1)),
                      EnqueueCallback<2>(&socket_cbs),
                      Return(net::ERR_IO_PENDING)))
      .RetiresOnSaturation();
  // Read final byte.
  EXPECT_CALL(mock_socket_, Read(NotNull(), 1, _))
      .WillOnce(DoAll(FillBufferFromString<0>(serialized_message.substr(
                          serialized_message.size() - 1, 1)),
                      EnqueueCallback<2>(&socket_cbs),
                      Return(net::ERR_IO_PENDING)))
      .RetiresOnSaturation();
  EXPECT_CALL(delegate_, OnMessage(EqualsProto(message)));
  transport_->StartReading();
  socket_cbs.Pop(MessageFramer::MessageHeader::header_size());
  socket_cbs.Pop(serialized_message.size() -
                 MessageFramer::MessageHeader::header_size() - 1);
  EXPECT_CALL(mock_socket_,
              Read(NotNull(), MessageFramer::MessageHeader::header_size(), _))
      .WillOnce(Return(net::ERR_IO_PENDING));
  socket_cbs.Pop(1);
}

TEST_F(CastTransportTest, TestReadErrorInHeaderAsync) {
  CompletionQueue socket_cbs;

  CastMessage message = CreateCastMessage();
  std::string serialized_message;
  EXPECT_TRUE(MessageFramer::Serialize(message, &serialized_message));

  // Read bytes [0, 3].
  EXPECT_CALL(mock_socket_,
              Read(NotNull(), MessageFramer::MessageHeader::header_size(), _))
      .WillOnce(DoAll(FillBufferFromString<0>(serialized_message),
                      EnqueueCallback<2>(&socket_cbs),
                      Return(net::ERR_IO_PENDING)))
      .RetiresOnSaturation();

  EXPECT_CALL(delegate_, OnError(CHANNEL_ERROR_SOCKET_ERROR, _));
  transport_->StartReading();
  // Header read failure.
  socket_cbs.Pop(net::ERR_CONNECTION_RESET);
}

TEST_F(CastTransportTest, TestReadErrorInBodyAsync) {
  CompletionQueue socket_cbs;

  CastMessage message = CreateCastMessage();
  std::string serialized_message;
  EXPECT_TRUE(MessageFramer::Serialize(message, &serialized_message));

  // Read bytes [0, 3].
  EXPECT_CALL(mock_socket_,
              Read(NotNull(), MessageFramer::MessageHeader::header_size(), _))
      .WillOnce(DoAll(FillBufferFromString<0>(serialized_message),
                      EnqueueCallback<2>(&socket_cbs),
                      Return(net::ERR_IO_PENDING)))
      .RetiresOnSaturation();
  // Read bytes [4, n-1].
  EXPECT_CALL(mock_socket_,
              Read(NotNull(),
                   serialized_message.size() -
                       MessageFramer::MessageHeader::header_size(),
                   _))
      .WillOnce(DoAll(FillBufferFromString<0>(serialized_message.substr(
                          MessageFramer::MessageHeader::header_size(),
                          serialized_message.size() -
                              MessageFramer::MessageHeader::header_size() - 1)),
                      EnqueueCallback<2>(&socket_cbs),
                      Return(net::ERR_IO_PENDING)))
      .RetiresOnSaturation();
  EXPECT_CALL(delegate_, OnError(CHANNEL_ERROR_SOCKET_ERROR, _));
  transport_->StartReading();
  // Header read is OK.
  socket_cbs.Pop(MessageFramer::MessageHeader::header_size());
  // Body read fails.
  socket_cbs.Pop(net::ERR_CONNECTION_RESET);
}

TEST_F(CastTransportTest, TestReadCorruptedMessageAsync) {
  CompletionQueue socket_cbs;

  CastMessage message = CreateCastMessage();
  std::string serialized_message;
  EXPECT_TRUE(MessageFramer::Serialize(message, &serialized_message));

  // Corrupt the serialized message body(set it to X's).
  for (size_t i = MessageFramer::MessageHeader::header_size();
       i < serialized_message.size();
       ++i) {
    serialized_message[i] = 'x';
  }

  // Read bytes [0, 3].
  EXPECT_CALL(mock_socket_,
              Read(NotNull(), MessageFramer::MessageHeader::header_size(), _))
      .WillOnce(DoAll(FillBufferFromString<0>(serialized_message),
                      EnqueueCallback<2>(&socket_cbs),
                      Return(net::ERR_IO_PENDING)))
      .RetiresOnSaturation();
  // Read bytes [4, n].
  EXPECT_CALL(mock_socket_,
              Read(NotNull(),
                   serialized_message.size() -
                       MessageFramer::MessageHeader::header_size(),
                   _))
      .WillOnce(DoAll(FillBufferFromString<0>(serialized_message.substr(
                          MessageFramer::MessageHeader::header_size(),
                          serialized_message.size() -
                              MessageFramer::MessageHeader::header_size() - 1)),
                      EnqueueCallback<2>(&socket_cbs),
                      Return(net::ERR_IO_PENDING)))
      .RetiresOnSaturation();

  EXPECT_CALL(delegate_, OnError(CHANNEL_ERROR_INVALID_MESSAGE, _));
  transport_->StartReading();
  socket_cbs.Pop(MessageFramer::MessageHeader::header_size());
  socket_cbs.Pop(serialized_message.size() -
                 MessageFramer::MessageHeader::header_size());
}

// ----------------------------------------------------------------------------
// Synchronous read tests
TEST_F(CastTransportTest, TestFullReadSync) {
  InSequence s;
  CastMessage message = CreateCastMessage();
  std::string serialized_message;
  EXPECT_TRUE(MessageFramer::Serialize(message, &serialized_message));

  // Read bytes [0, 3].
  EXPECT_CALL(mock_socket_,
              Read(NotNull(), MessageFramer::MessageHeader::header_size(), _))
      .WillOnce(DoAll(FillBufferFromString<0>(serialized_message),
                      Return(MessageFramer::MessageHeader::header_size())))
      .RetiresOnSaturation();
  // Read bytes [4, n].
  EXPECT_CALL(mock_socket_,
              Read(NotNull(),
                   serialized_message.size() -
                       MessageFramer::MessageHeader::header_size(),
                   _))
      .WillOnce(DoAll(FillBufferFromString<0>(serialized_message.substr(
                          MessageFramer::MessageHeader::header_size(),
                          serialized_message.size() -
                              MessageFramer::MessageHeader::header_size())),
                      Return(serialized_message.size() -
                             MessageFramer::MessageHeader::header_size())))
      .RetiresOnSaturation();
  EXPECT_CALL(delegate_, OnMessage(EqualsProto(message)));
  // Async result in order to discontinue the read loop.
  EXPECT_CALL(mock_socket_,
              Read(NotNull(), MessageFramer::MessageHeader::header_size(), _))
      .WillOnce(Return(net::ERR_IO_PENDING));
  transport_->StartReading();
}

TEST_F(CastTransportTest, TestPartialReadSync) {
  InSequence s;

  CastMessage message = CreateCastMessage();
  std::string serialized_message;
  EXPECT_TRUE(MessageFramer::Serialize(message, &serialized_message));

  // Read bytes [0, 3].
  EXPECT_CALL(mock_socket_,
              Read(NotNull(), MessageFramer::MessageHeader::header_size(), _))
      .WillOnce(DoAll(FillBufferFromString<0>(serialized_message),
                      Return(MessageFramer::MessageHeader::header_size())))
      .RetiresOnSaturation();
  // Read bytes [4, n-1].
  EXPECT_CALL(mock_socket_,
              Read(NotNull(),
                   serialized_message.size() -
                       MessageFramer::MessageHeader::header_size(),
                   _))
      .WillOnce(DoAll(FillBufferFromString<0>(serialized_message.substr(
                          MessageFramer::MessageHeader::header_size(),
                          serialized_message.size() -
                              MessageFramer::MessageHeader::header_size() - 1)),
                      Return(serialized_message.size() -
                             MessageFramer::MessageHeader::header_size() - 1)))
      .RetiresOnSaturation();
  // Read final byte.
  EXPECT_CALL(mock_socket_, Read(NotNull(), 1, _))
      .WillOnce(DoAll(FillBufferFromString<0>(serialized_message.substr(
                          serialized_message.size() - 1, 1)),
                      Return(1)))
      .RetiresOnSaturation();
  EXPECT_CALL(delegate_, OnMessage(EqualsProto(message)));
  EXPECT_CALL(mock_socket_,
              Read(NotNull(), MessageFramer::MessageHeader::header_size(), _))
      .WillOnce(Return(net::ERR_IO_PENDING));
  transport_->StartReading();
}

TEST_F(CastTransportTest, TestReadErrorInHeaderSync) {
  InSequence s;
  CastMessage message = CreateCastMessage();
  std::string serialized_message;
  EXPECT_TRUE(MessageFramer::Serialize(message, &serialized_message));

  // Read bytes [0, 3].
  EXPECT_CALL(mock_socket_,
              Read(NotNull(), MessageFramer::MessageHeader::header_size(), _))
      .WillOnce(DoAll(FillBufferFromString<0>(serialized_message),
                      Return(net::ERR_CONNECTION_RESET)))
      .RetiresOnSaturation();
  EXPECT_CALL(delegate_, OnError(CHANNEL_ERROR_SOCKET_ERROR, _));
  transport_->StartReading();
}

TEST_F(CastTransportTest, TestReadErrorInBodySync) {
  CastMessage message = CreateCastMessage();
  std::string serialized_message;
  EXPECT_TRUE(MessageFramer::Serialize(message, &serialized_message));

  // Read bytes [0, 3].
  EXPECT_CALL(mock_socket_,
              Read(NotNull(), MessageFramer::MessageHeader::header_size(), _))
      .WillOnce(DoAll(FillBufferFromString<0>(serialized_message),
                      Return(MessageFramer::MessageHeader::header_size())))
      .RetiresOnSaturation();
  // Read bytes [4, n-1].
  EXPECT_CALL(mock_socket_,
              Read(NotNull(),
                   serialized_message.size() -
                       MessageFramer::MessageHeader::header_size(),
                   _))
      .WillOnce(DoAll(FillBufferFromString<0>(serialized_message.substr(
                          MessageFramer::MessageHeader::header_size(),
                          serialized_message.size() -
                              MessageFramer::MessageHeader::header_size() - 1)),
                      Return(net::ERR_CONNECTION_RESET)))
      .RetiresOnSaturation();
  EXPECT_CALL(delegate_, OnError(CHANNEL_ERROR_SOCKET_ERROR, _));
  transport_->StartReading();
}

TEST_F(CastTransportTest, TestReadCorruptedMessageSync) {
  InSequence s;
  CastMessage message = CreateCastMessage();
  std::string serialized_message;
  EXPECT_TRUE(MessageFramer::Serialize(message, &serialized_message));

  // Corrupt the serialized message body(set it to X's).
  for (size_t i = MessageFramer::MessageHeader::header_size();
       i < serialized_message.size();
       ++i) {
    serialized_message[i] = 'x';
  }

  // Read bytes [0, 3].
  EXPECT_CALL(mock_socket_,
              Read(NotNull(), MessageFramer::MessageHeader::header_size(), _))
      .WillOnce(DoAll(FillBufferFromString<0>(serialized_message),
                      Return(MessageFramer::MessageHeader::header_size())))
      .RetiresOnSaturation();
  // Read bytes [4, n].
  EXPECT_CALL(mock_socket_,
              Read(NotNull(),
                   serialized_message.size() -
                       MessageFramer::MessageHeader::header_size(),
                   _))
      .WillOnce(DoAll(FillBufferFromString<0>(serialized_message.substr(
                          MessageFramer::MessageHeader::header_size(),
                          serialized_message.size() -
                              MessageFramer::MessageHeader::header_size() - 1)),
                      Return(serialized_message.size() -
                             MessageFramer::MessageHeader::header_size())))
      .RetiresOnSaturation();
  EXPECT_CALL(delegate_, OnError(CHANNEL_ERROR_INVALID_MESSAGE, _));
  transport_->StartReading();
}
}  // namespace cast_channel
}  // namespace core_api
}  // namespace extensions
