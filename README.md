# Intro
大二的时候做过这个项目，当时做得欲仙欲死。四年之后再来做，感慨万千啊。


# Lab1
唯一难度就是at-most-once，同时保证FIFO，避免重复的旧的请求发生在新的请求之后。两者都可以通过Seq实现。

## 编程模型
+ 多client，单client会并发，请求fifo: 暂时不考虑复杂的并发控制
+ 单server

## 错误模型
+ client：不会失效
+ server：不会失效
+ network：不会出问题

## 任务
+ lock server语义
+ rpc lib at-most-once：比较恶心的是，client是并发的，所以请求不是fifo的，不然记录一个ack#就行了
    + 由client显式ack
    + guideline里面给出了一个非常好的问题: What if a retransmitted request arrives while the server is still processing the original request?
        + 解决这一问题，意味着要在应用层做去重。

## 难点
+ 理清语义
    + 同一个client并发拿同一把锁，语义是怎么样的？
    + 一把锁被释放了两次，是否是正确的行为？
