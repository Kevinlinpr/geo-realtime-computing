collector 的数据经过 server 中转分发到 cuda , cuda 进行耗时计算后将计算结果经过 server 中转返回给 collector



collector 链接服务器 

上传操作时
询问cuda是否在线

上传任务信息并等待上传指令
如果在线 上传数据
如果不在线等待

如果上传数据的时候失败，从新开始



cuda 链接服务器
获取任务信息，开启任务，响应
接受数据
计算
返回数据，如果返回失败 重新获取任务

