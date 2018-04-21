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
+ rpctest里面测试了arg过少与过多的情况，然而，rpc库没有处理，guideline里面也没有提
+ 又采了一个坑，client的并发请求会导致reply window中的请求无序
    + client端并发时，拿号与发送是两个非原子的过程，这会造成a拿了号1，b拿了号2，然后b先send成功
    + 解决方法要么是client端加锁，保证请求是fifo的，要么在server端实现复杂的sliding window机制
+ 我是从github上某个repo下载的代码，不过感觉代码有些问题，所以rpc.cc中其他的部分也需要改动

## 还未解决的Bug
+ server收到client ack时，会清除reply中的buffer，但此buffer现在可能正在被其他人使用。懒得改了
+ server收到client ack时，移动window，但实际上，仍然有可能有pending的重复请求在之后到来，此时，会直接返回给client atmostonce-error
