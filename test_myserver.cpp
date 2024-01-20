#include <gtest/gtest.h>
#include "myserver.cpp"
#include <gmock/gmock.h>

class MockClient {
public:
    MOCK_METHOD(int, send, (int, const void*, size_t, int), ());
    MOCK_METHOD(int, recv, (int, void*, size_t, int), ());
};

TEST(ProcessSendTest, PositiveTest) {
    MockClient mockClient;
    EXPECT_CALL(mockClient, send(_, _, _, _)).Times(AnyNumber()); 
    EXPECT_CALL(mockClient, recv(_, _, _, _)).Times(AnyNumber()); 
    int result = processSend(mockClient);

    // Assert the result
    EXPECT_EQ(result, 1);
}

TEST(ProcessSendTest, NegativeTest) {
    MockClient mockClient;

    EXPECT_CALL(mockClient, send(_, _, _, _)).Times(AnyNumber());  
    EXPECT_CALL(mockClient, recv(_, _, _, _)).Times(AnyNumber());
    int result = processSend(mockClient);

    // Assert the result
    EXPECT_EQ(result, -1);
}

TEST(ProcessSendTest, AnotherTest) {
    MockClient mockClient;

    EXPECT_CALL(mockClient, send(_, _, _, _)).Times(AnyNumber());
    EXPECT_CALL(mockClient, recv(_, _, _, _)).Times(AnyNumber());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}