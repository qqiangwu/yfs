# Intro
大二的时候做过这个项目，当时做得欲仙欲死。四年之后再来做，感慨万千啊。

顺便玩玩C++17。

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

# Lab2
## ExtentServer
这就是一个BlockDevice啊。嗯，为了简单，现在也实现一个内存态的吧。精力主要集中在yfs，就当是存储计算分离好了。

## Yfs
基于extent server实现一个可靠的yfs还是挺难的。考虑以下几个问题

+ yfs执行一个操作，需要更新多个extent，在更新第一个extent成功后，extent server挂了，或者yfs挂了，怎么办？
    + 实际上，并没有很好的解决方法，这是文件系统必须要面对的事情。实际的系统中，要么调整写的顺序，来降低风险；要么提供某种原子提交的机制
    + 在目前的lab中，不需要考虑这个问题，还是挺幸福的
+ 多个线程并发访问一个yfs实例：当前lab中没有这个问题，这个问题相对下一个问题更加容易解决一些
+ 多个yfs并发访问一个extent server：这就意味着，yfs实例不能缓存extent。实际上，如果多个yfs实例并发访问的话，需要做并发控制，要么用CAS，要么用锁
    + 目前的实验中没有这一问题
+ Lab中没有显式说明extent的大小，如果是BlockDevice抽象的话，extent大小应该是固定的。目前，为了简单，先实现为extent为变长的。则，每个文件与目录，都可以实现为一个extent。

# Lab3
由于需要单实例并发和多实例并行，所以所有操作加锁就行了。

+ 锁的粒度：我们以目录和文件为粒度加锁，如果unlink时，需要先对目录加锁，再对文件加锁。由于文件系统存在天然的层次结构，所以按层次加锁不会出现死锁问题。
+ 容错：由于不需要考虑锁服务器会挂的情况，所以代码真的简单好多。

闲着没事测试了一下性能，果然这种N次switch+M次rr的性能不能看。

```
Jobs: 1 (f=1): [r] [100.0% done] [464KB/0KB/0KB /s] [116/0/0 iops] [eta 00m:00s]
random-read: (groupid=0, jobs=1): err= 0: pid=28749: Mon Apr 30 15:44:24 2018
  read : io=8192.0KB, bw=472810B/s, iops=115, runt= 17742msec
    clat (usec): min=7764, max=12903, avg=8642.99, stdev=261.70
     lat (usec): min=7765, max=12904, avg=8643.38, stdev=261.69
    clat percentiles (usec):
     |  1.00th=[ 7968],  5.00th=[ 8096], 10.00th=[ 8256], 20.00th=[ 8512],
     | 30.00th=[ 8640], 40.00th=[ 8640], 50.00th=[ 8768], 60.00th=[ 8768],
     | 70.00th=[ 8768], 80.00th=[ 8768], 90.00th=[ 8896], 95.00th=[ 8896],
     | 99.00th=[ 8896], 99.50th=[ 9024], 99.90th=[10176], 99.95th=[10432],
     | 99.99th=[12864]
    bw (KB  /s): min=  441, max=  471, per=100.00%, avg=461.91, stdev= 4.32
    lat (msec) : 10=99.85%, 20=0.15%
  cpu          : usr=0.25%, sys=0.38%, ctx=2052, majf=0, minf=5
  IO depths    : 1=100.0%, 2=0.0%, 4=0.0%, 8=0.0%, 16=0.0%, 32=0.0%, >=64=0.0%
     submit    : 0=0.0%, 4=100.0%, 8=0.0%, 16=0.0%, 32=0.0%, 64=0.0%, >=64=0.0%
     complete  : 0=0.0%, 4=100.0%, 8=0.0%, 16=0.0%, 32=0.0%, 64=0.0%, >=64=0.0%
     issued    : total=r=2048/w=0/d=0, short=r=0/w=0/d=0

Run status group 0 (all jobs):
   READ: io=8192KB, aggrb=461KB/s, minb=461KB/s, maxb=461KB/s, mint=17742msec, maxt=17742msec
```
