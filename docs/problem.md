## epoll 的边缘触发和水平触发什么区别？
设置了水平触发 + 读事件，文件描述符只要缓冲区里有数据，就一直放在绪链表里，直到用户调用 epoll_ctl() 删除这个文件描述符或者修改这个文件描述符的事件。
设置了边缘触发 + 读事件，文件描述符只在新数据来的时候，放到就绪链表里。

## listen() 的 backlog 参数什么作用？
这个 backlog 设定了内个的 accept 队列的最大值，当 accept 队列满了后，OS 拒绝新的连接。
当用户调用 accept() 拿到一个队列后，OS 的 accept 队列才空一个位置。
内核有 backlog 的默认值，用户也可以通过 sysctl -w net.core.somaxconn=1024 来修改 backlog 的默认值。
当用户设定的 backlog 大于内核默认值时，内核会使用默认值。

## 当内核的 epoll 红黑树事件有 2048 个事件都放到了就绪链表里，但是 epoll_wait() 设置了 max_events 是 1024，epoll_wait() 剩下的 1024 个事件怎么办 ？
剩下的 1024 个事件继续留在就绪链表里，等下一次 epoll_wait() 调用时在移除。

## 当 epoll 使用了边缘触发模式，一个读事件触发了，我没有读完所有内核缓冲区里的数据，以后这个文件描述符的读事件还会触发吗？会不会一辈子都不触发了？

边缘触发模式下，只要客户端新数据来到，那么不管之前的数据有没有读完，都会讲这个文件描述符放到就绪队列里面。
所以后续会触发。

## 非阻塞下怎么维护每一个连接？
用 fcntl() 设置了客户端文件描述符和服务端文件描述符为非阻塞。  
当我用 recv() 接收客户端数据时可能只会读完内核缓冲区的数据，而客户端的数据没有读完。  
这次的 epoll 事件已经使用了，需要再把这个客户端文件描述符放到 epoll 里。
我用了线程池处理连接，这次的线程和下一次的线程可能不是一个线程，所以已经接收的客户端部分数据需要放在一个地方。  
所有线程都能访问到这个地方，用来以后读当前客户端剩下的数据。  
所以需要维护一个全局哈希表，存(客户端文件描述符->[数据缓冲区 + 数据是否读取完毕 + 状态信息])。  
如果读取完毕所有客户端数据，才可以继续处理 HTTP 请求。

## 维护每一个连接的 map 怎么加锁？
整个 map 加锁还是，每一个的键值对加锁？  
主线程向 map 里加连接，几个子线程读和删连接。  
若是整个 map 加锁，主线程加连接时，子线程都要等。  
...  

## epoll 边缘触发 + 客户端非阻塞时，怎么获取完整请求？
若完整请求是：  

{  
GET \ HTTP\1.0\r\n  
Host: example.com\r\n  
User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n  
Accept: text/html,application/xhtml+xml\r\n  
Accept-Language: zh-CN,zh;q=0.9\r\n  
Connection: keep-alive\r\n  
\r\n  
}  

当循环 recv() 接收客户端数据时，可能内核的 recv 缓冲区内只有:  
{  
GET \ HTTP\1.0\r\n  
Host: example.com\r\n  
User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n  
Accept: text/html,appli  
}  

fcntl(fd, F_SETFL, O_NONBLOCK, -1) 设置了非阻塞.  
epoll_ctl(epoll_fd, EPOLL_CTL_ADD, EPOLLIN | EPOLLET, client_fd, &event) 设置了边缘触发。  
设置了这两个后 recv() 可能只读完了这一部分 recv 缓冲区里的所有数据，剩下的数据还没有到，recv() 返回 -1，和 (errno == EAGAIN || errno == EWOULDBLOCK)。  
这个 -1 和 errno 只能确定 recv 缓冲区里没有数据了，不能确定完整 HTTP 请求。  

要确定完整 HTTP 请求，需要每次在 while 循环中 recv 了当前数据之后，判断请求是否完整。  
HTTP 请求分两类：一类是只有请求头，一类是请求头 + 请求体。  
只有请求头：  
{  
GET \ HTTP\1.0\r\n  
Host: example.com\r\n  
User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n  
Accept: text/html,application/xhtml+xml\r\n  
Accept-Language: zh-CN,zh;q=0.9\r\n  
Connection: keep-alive\r\n  
\r\n  
}  
请求头 + 请求体：  
{  
POST /login HTTP/1.1\r\n  
Host: example.com\r\n  
Content-Type: application/x-www-form-urlencoded\r\n  
Content-Length: 29\r\n  
\r\n  
username=alice&password=123456  
}  
所以判断请求完整的情况：数据中有 \r\n\r\n 能确定有了整个头，请求头中有 Content-Length 能确定是请求体大小。  
根据 Content-Length 数据的完整。

所以判断数据完整的大致流程：  
```c
if(有 \r\n\r\n) {  
    if(有 Content-Length) {  
        if(\r\n\r\n 之后的数据总共大小 == Content-Length) {  
            return 完整；  
        }  
        return 不完整；  
    }  
    return 完整；  
}  
return 不完整

```

## 解析 HTTP 字符串时，原始字符串还在，为了加快速度，怎么避免多复制一份到 map 里？


