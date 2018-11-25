#include "sysinclude.h" 
#include <iostream>
#include <vector>
#include <queue>
#include <string.h>
#include <assert.h>
#include <list>
#include <fstream>

using namespace std;

ofstream fout("slide_window.txt");


/*
系统提供的接口函数——发送帧函数
参数：
	pData：指向要发送的帧的内容的指针
	len：要发送的帧的长度
 */
extern void SendFRAMEPacket(unsigned char* pData, unsigned int len); 

#define WINDOW_SIZE_STOP_WAIT 1

#define WINDOW_SIZE_BACK_N_FRAME 4 



/*
pBuffer指向的数据的结构如以下代码中frame结构的定义
 */
typedef enum {data,ack,nak} frame_kind; 

typedef struct frame_head{

	frame_kind kind;			//帧类型
	unsigned int seq;			//序列号
	unsigned int ack;			//确认号
	unsigned char data[100];	//数据

};

typedef struct frame {

	frame_head head; 				//帧头
	unsigned int size; 				//数据的大小 

};


typedef struct Buffer{

	frame* pFrame;
	unsigned char* pBuffer;
	unsigned int leng;

}



/*

* 停等协议测试函数
*
* 1-bit滑动窗口
* pBuffer:指针，指向系统要发送或接受到的帧内容，或者指向超时信息中超时帧的序号内容
* bufferSize：pBuffer表示内容的长度，为字节数
* messageType：传入的消息类型，可以为以下几种情况
*
*返回值：
*	0表示成功
*	-1表示失败
*
* 
* 	MSG_TYPE_TIMEOUT	某个帧超时，需要根据帧的序号将该帧及后面的帧重新发送
* 	MSG_TYPE_SEND		系统要发送一个帧：缓存到发送队列中，当发送窗口没有满的时候，
* 						打开一个新的窗口发送这个帧，否则就返回并且进入等待状态
*  	MSG_TYPE_RECEIVE	系统接收到一个帧的ACK，检查ACK的值，并且将其对应的窗口关闭
*  
*  	
*/



int stud_slide_window_stop_and_wait(char *pBuffer, int bufferSize, UINT8 messageType)

{

	/*
	为了区分已发送待确认的帧以及在滑动窗口满的情况下还不能进入窗口发送的等待帧，定义两个队列
	wait_frame_queue:	暂时无法发送的帧，因为滑动窗口都满了，需要将这一类的帧都缓存在等待队列中
	confirm_frame_queue:	已经发送，但是还没有接收到接受方发来的确认帧的帧，需要缓存在待确认帧队列中
	 */

	
	static queue<frame> wait_frame_queue;
	static vector<frame> confirm_frame_queue;
	static unsigned int expectedNum = 0;	//记录一下当前期望收到的帧序列号

	switch(messageType)
	{
		case MSG_TYPE_SEND:
		{
			frame sendFrame;
			memcpy((void*)&sendFrame.head, (void*)pBuffer, bufferSize);
			sendFrame.size = bufferSize;

			if(wait_frame_queue.empty() == false)//当前等待队列不为空，因此滑动窗口肯定也满了，因此需要将当前要发送的帧缓存到等待队列中
			{
				wait_frame_queue.push(sendFrame);
				return 0;
			}

			if(wait_frame_queue.empty() == true && confirm_frame_queue.empty() == false)
				//当前等待队列为空，但是当前的滑动窗口不为空，又因为是停等式滑动窗口，窗口大小为1，因此也需要将当前要发送的帧缓存到等待队列中
			{
				wait_frame_queue.push(sendFrame);
				return 0;
			}

			//调用发送函数
			SendFRAMEPacket((unsigned char*)&sendFrame, bufferSize);

			//记录一下当前发出去的帧序号
			expectedNum = sendFrame.head.seq;

			//将当前发送的帧加到待确认队列中
			confirm_frame_queue.push_back(sendFrame);
			return 0;
		}

		case MSG_TYPE_RECEIVE:
		{
			frame_head *headPointer = (frame_head *)pBuffer;

			if(headPointer->ack != expectedNum || confirm_frame_queue.size() == 0)
			{
				//如果当前收到的确认帧序号不等于期望中的帧序号，或者没有需要确认的帧时，代表出错
				return -1;
			}

			confirm_frame_queue.erase(confirm_frame_queue.begin());
			//其实在停等式协议中，confirm_frame_queue队列中要么空要么只有一个帧待确认
			//因此进行到这里的时候，说明发送方成功收到接收方发送的确认帧，因此需要看等待队列，如果等待队列为空，则直接返回
			//如果等待队列非空，那么需要从等待队列中弹出一个帧进行发送
			//
			if(wait_frame_queue.size() == 0)
				return 0;
			else
			{
				frame sendFrame = wait_frame_queue.front();
				wait_frame_queue.pop();
				SendFRAMEPacket((unsigned char*) &sendFrame, sendFrame.size);
				confirm_frame_queue.push_back(sendFrame);
				expectedNum = sendFrame.head.seq;
				return 0;
			}
		}

		case MSG_TYPE_TIMEOUT:
		{
			//如果收到了超时，但是没有需要待确认的帧的时候，代表出错了
			if(confirm_frame_queue.size() == 0)
				return -1;
			//否则从待确认帧队列中找出唯一的帧，重新发送
			frame sendFrame = confirm_frame_queue.front();
			SendFRAMEPacket((unsigned char*) &sendFrame, sendFrame.size);
			return 0;
		}
		default:
			return -1;
	}
	return 0;

} 

/*

* 回退n帧测试函数
* 参数类型定义同上

*/

int stud_slide_window_back_n_frame(char *pBuffer, int bufferSize, UINT8 messageType)

{
	static queue<frame> wait_frame_queue;
	static vector<frame> confirm_frame_queue;

	switch(messageType)
	{
		case MSG_TYPE_SEND:
		{
			frame sendFrame;
			memcpy( (void*)&sendFrame.head, (void*)pBuffer, bufferSize);
			sendFrame.size = bufferSize;

			//如果当前等待队列非空，那么现在要发送的帧只能进入等待队列
			if(wait_frame_queue.empty() == false)
			{
				wait_frame_queue.push(sendFrame);
				return 0;
			}

			//如果当前等待队列为空，但是待确认帧队列已经满了，那么这时候当前要发送的帧也必须进入等待队列中
			if(wait_frame_queue.empty() == true && confirm_frame_queue.size() == WINDOW_SIZE_BACK_N_FRAME)
			{
				wait_frame_queue.push(sendFrame);
				return 0;
			}

			//余下的情况表示等待队列为空，并且滑动窗口还没满，也就是待确认队列还没有满，那么就发送当前帧，并且缓存到待确认队列中
			SendFRAMEPacket((unsigned char*) &sendFrame, bufferSize);
			confirm_frame_queue.push_back(sendFrame);
			return 0;
		}

		case MSG_TYPE_RECEIVE:
		{
			frame_head *headPointer = (frame_head *)pBuffer;
			//查找收到的确认ack对应的frame
			vector<frame>::iterator cur_confirm_frame;
			for(cur_confirm_frame = confirm_frame_queue.begin(); cur_confirm_frame != confirm_frame_queue.end(); cur_confirm_frame++)
			{
				if(cur_confirm_frame->head.seq == headPointer->ack)
					break;
			}

			//没有在待确认帧队列中找到ack对于的帧
			if(cur_confirm_frame == confirm_frame_queue.end())
				return -1;

			//收到确认帧，表示该帧之前的全部帧都已经接受到了，全部从待确认队列中除去
			while(cur_confirm_frame >= confirm_frame_queue.begin())
			{
				confirm_frame_queue.erase(cur_confirm_frame);
				cur_confirm_frame --;
			}

			if(wait_frame_queue.size() == 0)
				return 0;

			//如果当前待确认帧队列还没有满，也就是滑动窗口没有满，就将等待帧队列中的帧加到待确定队列中，并发送，知道填满滑动窗口
			while(confirm_frame_queue.size() < WINDOW_SIZE_BACK_N_FRAME)
			{
				frame sendFrame = wait_frame_queue.front();
				wait_frame_queue.pop();
				SendFRAMEPacket((unsigned char*) &sendFrame, sendFrame.size);
				confirm_frame_queue.push_back(sendFrame);
			}
			return 0;
		}

		case MSG_TYPE_TIMEOUT:
		{
			if(confirm_frame_queue.size() == 0)
				return -1;
			unsigned int lostSeq = ntohl(*(unsigned int *)pBuffer);
			vector<frame>::iterator cur_lost_frame;
			bool flag = false;
			for(cur_lost_frame = confirm_frame_queue.begin(); cur_lost_frame != confirm_frame_queue.end(); cur_lost_frame++)
			{
				if(cur_lost_frame->head.seq == lostSeq)
				{
					flag = true;
					break;
				}
			}

			if(flag)
			{
				cur_lost_frame = confirm_frame_queue.begin();
				//如果找到超时的帧，那么将此帧之后的所有帧都重新发送
				while(cur_lost_frame < confirm_frame_queue.end())
				{
					SendFRAMEPacket((unsigned char*)&(*cur_lost_frame), cur_lost_frame->size);
					cur_lost_frame ++ ;
				}
			}
			
			return 0;
		}

		default:
			return -1;
	}

    return 0;

} 

/*

* 选择性重传测试函数
* 参数类型定义同上

*/

int stud_slide_window_choice_frame_resend(char *pBuffer, int bufferSize, UINT8 messageType)

{
	static queue<frame> wait_frame_queue;
	static vector<frame> confirm_frame_queue;

	switch(messageType)
	{
		case MSG_TYPE_SEND:
		{
			frame sendFrame;
			memcpy((void*)& sendFrame.head, (void*) pBuffer, bufferSize);
			sendFrame.size = bufferSize;

			if(wait_frame_queue.empty() == false)
			{
				wait_frame_queue.push(sendFrame);
				return 0;
			}
			if(wait_frame_queue.empty() == true && confirm_frame_queue.size() == WINDOW_SIZE_BACK_N_FRAME)
			{
				wait_frame_queue.push(sendFrame);
				return 0;
			}
			SendFRAMEPacket((unsigned char*) &sendFrame, bufferSize);
			confirm_frame_queue.push_back(sendFrame);
			return 0;
		}
		case MSG_TYPE_RECEIVE:
		{
			frame_head *headPointer = (frame_head*) pBuffer;
			frame_kind kind = (frame_kind)ntohl(headPointer->kind);
			vector<frame>::iterator cur_confirm_frame;

			for(cur_confirm_frame = confirm_frame_queue.begin(),cur_confirm_frame != confirm_frame_queue.end(), cur_confirm_frame++)
			{
				if(cur_confirm_frame->head.seq == headPointer->ack)
					break;
			}

			if(cur_confirm_frame == confirm_frame_queue.end())
				return -1;

			if(kind == nak)
				SendFRAMEPacket((unsigned char*)&(*cur_confirm_frame), cur_confirm_frame->size);
			else if(kind == ack)
			{
				while(cur_confirm_frame >= confirm_frame_queue.begin())
				{
					confirm_frame_queue.erase(cur_confirm_frame);
					cur_confirm_frame --;
				}
				if(wait_frame_queue.size() == 0)
					return 0;
				while(confirm_frame_queue.size() < WINDOW_SIZE_BACK_N_FRAME)
				{
					frame sendFrame = wait_frame_queue.front();
					wait_frame_queue.pop();
					SendFRAMEPacket((unsigned char*)&sendFrame, sendFrame.size);
					confirm_frame_queue.push_back(sendFrame);
				}
			}
			return 0;
		}
		case MSG_TYPE_TIMEOUT:
		{
			if(confirm_frame_queue.size() == 0)
				return -1;
			unsigned int lostSeq = ntohl(*(unsigned int *)pBuffer);
			vector<frame>::iterator cur_lost_frame;
			for(cur_lost_frame = confirm_frame_queue.begin(); cur_lost_frame != confirm_frame_queue.end(); cur_lost_frame ++)
			{
				if(cur_lost_frame->head.seq == lostSeq)
					break;
			}
			if(cur_lost_frame == confirm_frame_queue.end())
				return -1;

			//选择性重传只需要重传当前超时的帧就可以
			SendFRAMEPacket((unsigned char*)&(*cur_lost_frame), cur_lost_frame->size);
			return 0;
		}
		default:
			return -1;
	}
    
    return 0;

}