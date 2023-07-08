BOCHS = bochs -f
BOCHSPATH = /home/vir-bryanq-dylan/VMs/bochs/OS/
DD = of=/home/vir-bryanq-dylan/VMs/bochs/OS/hd60M.img bs=512 conv=notrunc
OBJS = build/main.o build/init.o build/interrupt.o build/kernel.o build/print.o build/timer.o build/debug.o build/string.o \
build/bitmap.o build/memory.o build/thread.o build/list.o build/switch.o build/sync.o build/console.o build/keyboard.o \
build/ioqueue.o build/tss.o build/process.o build/syscall.o build/stdio.o build/ide.o build/fs.o build/inode.o build/dir.o \
build/file.o build/exec.o build/_syscall.o build/pipe.o
INCLUDE = -I lib/kernel/ -I kernel/ -I boot/include -I device/ -I lib/ -I thread/ -I userprog/ -I lib/user/ -I fs/
CFLAGS = -c -m32 -fno-stack-protector  -fno-builtin -Wmissing-prototypes -Wstrict-prototypes -Wall $(INCLUDE) 
CC = gcc
LDFLAGS = -Ttext 0xc0001500 -e main -m elf_i386
MDS = build/mbr.bin build/loader.bin build/kernel.bin

write: image
# 写入磁盘文件
	dd if=build/mbr.bin $(DD)
	dd if=build/loader.bin $(DD) seek=1
	dd if=build/kernel.bin $(DD) seek=5

image: $(MDS)

# mbr模块
build/mbr.bin: boot/mbr.s
	nasm -f bin -o $@ $< $(INCLUDE)

# loader模块
build/loader.bin: boot/loader.s
	nasm -f bin -o $@ $^ $(INCLUDE)

# kernel模块
build/kernel.bin: $(OBJS)
	ld -o $@ $^ $(LDFLAGS)    

build/main.o: kernel/main.c
	$(CC) -o $@ $^ $(CFLAGS)

build/init.o: kernel/init.c
	$(CC) -o $@ $^ $(CFLAGS)

build/interrupt.o: kernel/interrupt.c
	$(CC) -o $@ $^ $(CFLAGS)

build/timer.o: device/timer.c
	$(CC) -o $@ $^ $(CFLAGS)

build/debug.o: kernel/debug.c
	$(CC) -o $@ $^ $(CFLAGS)

build/string.o: lib/string.c
	$(CC) -o $@ $^ $(CFLAGS)

build/bitmap.o: lib/kernel/bitmap.c
	$(CC) -o $@ $^ $(CFLAGS)

build/memory.o: kernel/memory.c
	$(CC) -o $@ $^ $(CFLAGS)

build/thread.o: thread/thread.c
	$(CC) -o $@ $^ $(CFLAGS)

build/list.o: lib/kernel/list.c
	$(CC) -o $@ $^ $(CFLAGS)

build/sync.o: thread/sync.c
	$(CC) -o $@ $^ $(CFLAGS)

build/console.o: device/console.c
	$(CC) -o $@ $^ $(CFLAGS)

build/keyboard.o: device/keyboard.c
	$(CC) -o $@ $^ $(CFLAGS)

build/ioqueue.o: device/ioqueue.c
	$(CC) -o $@ $^ $(CFLAGS)

build/tss.o: userprog/tss.c
	$(CC) -o $@ $^ $(CFLAGS)

build/process.o: userprog/process.c
	$(CC) -o $@ $^ $(CFLAGS)

build/syscall.o: lib/user/syscall.c
	$(CC) -o $@ $^ $(CFLAGS)

build/stdio.o: lib/stdio.c
	$(CC) -o $@ $^ $(CFLAGS)

build/ide.o: device/ide.c
	$(CC) -o $@ $^ $(CFLAGS)

build/fs.o: fs/fs.c
	$(CC) -o $@ $^ $(CFLAGS)

build/file.o: fs/file.c
	$(CC) -o $@ $^ $(CFLAGS)

build/dir.o: fs/dir.c
	$(CC) -o $@ $^ $(CFLAGS)

build/inode.o: fs/inode.c
	$(CC) -o $@ $^ $(CFLAGS)

build/exec.o: userprog/exec.c
	$(CC) -o $@ $^ $(CFLAGS)

build/_syscall.o: lib/user/_syscall.c
	$(CC) -o $@ $^ $(CFLAGS)

build/pipe.o: fs/pipe.c
	$(CC) -o $@ $^ $(CFLAGS)

build/kernel.o: kernel/kernel.s
	nasm -f elf -o $@ $^ 

build/print.o: lib/kernel/print.s
	nasm -f elf -o $@ $^ 

build/switch.o: thread/switch.s
	nasm -f elf -o $@ $^

disk: disk0 disk1 disk2

disk0:
	cd $(BOCHSPATH); rm -f ./hd80M.img; cp ./hd80M.img.bak ./hd80M.img;

disk1:
	cd $(BOCHSPATH); rm -f ./hd80M_1_m.img; cp ./hd80M.img.bak ./hd80M_1_m.img

disk2:
	cd $(BOCHSPATH); rm -f ./hd80M_1_s.img; cp ./hd80M.img.bak ./hd80M_1_s.img
	
run: 
	cd $(BOCHSPATH); $(BOCHS) bochsrc.disk

clean:
	$(RM) build/*