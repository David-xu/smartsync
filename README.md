# smartsync
远程文件同步工具，cs模型，client总是实时跟踪server端的所有变化，server端的增删修改等操作均被同步到client。
# 用法
./smartsync -h
Usage: ./smartsync [OPTION]
-h, --help           display this help and exit
-l, --list           display all file in dir
-p, --path           local path
-a, --address        server ip
-m, --match          match list
-i, --ignore         ignore list

-p指定本地路径，对于server端是待同步的源路径，client端则是目标路径
-a指定远端ip
-m和-i用于过滤文件，如不指定则是所有-p指定路径下所有的文件(递归包含所有的子文件夹)。
-l用于显示过滤后的结果
