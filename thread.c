// #define _XOPEN_SOURCE 700
#include "thread.h"
#include "init.h"
#include "queue.h"
#include "scheduler.h"
#include "sync.h"
#include <linux/sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define STACK_SIZE 1024 * 64

ThreadQueue ReadyQueue;
ThreadQueue WaitingQueue;
ThreadTblEnt pThreadTbEnt[MAX_THREAD_NUM];
Thread *pCurrentThread; // Running ������ Thread�� ����Ű�� ����

void Init(void) {
    /* ReadyQueue �ʱ�ȭ */
    ReadyQueue.queueCount = 0;
    ReadyQueue.pHead = NULL;
    ReadyQueue.pTail = NULL;

    /* WaitingQueue �ʱ�ȭ */
    WaitingQueue.queueCount = 0;
    WaitingQueue.pHead = NULL;
    WaitingQueue.pTail = NULL;

    pCurrentThread = NULL; // pCurrentThread �ʱ�ȭ

    /* pThreadTbEnt ��� �ʱ�ȭ */
    for (int i = 0; i < MAX_THREAD_NUM; i++) {
        pThreadTbEnt[i].bUsed = 0;
        pThreadTbEnt[i].pThread = NULL;
    }
}
int thread_create(thread_t *thread, thread_attr_t *attr,
                  void *(*start_routine)(void *), void *arg) {
    SEM = sem_open("mysem", O_CREAT, 0644, 1);
    sem_wait(SEM); // ���� ������ ���� ����ȭ

    Thread *new_thread;
    void *stack;
    thread_t tid;
    pid_t new_pid;
    BOOL is_full = 1;

    /* Table�� entry 0���� �� entry�� ã�´� */
    for (int i = 0; i < MAX_THREAD_NUM; i++) {
        if (pThreadTbEnt[i].bUsed == 0) {
            is_full = 0;
            tid = i;       // �ش� ��Ʈ���� ��ȣ�� thread�� id��.
            *thread = tid; // ������ thread id ��ȯ
            break;
        }
    }

    if (is_full == 1) { // Table�� �� ��Ʈ���� ���� ���
        perror("The number of creatable threads are full!");
        sem_post(SEM);
        return -1;
    }

    stack = malloc(STACK_SIZE);
    new_thread = (Thread *)malloc(sizeof(Thread));

    // ������ ���� �� pid ��ȯ
    new_pid = clone(start_routine, (char *)stack + STACK_SIZE,
                    CLONE_VM | CLONE_SIGHAND | CLONE_FS | CLONE_FILES, arg);

    kill(new_pid, SIGSTOP); // �����带 ���� �� �ٷ� ������Ų��

    /* TCB �ʱ�ȭ */
    new_thread->stackSize = STACK_SIZE;
    new_thread->stackAddr = stack;
    new_thread->status = THREAD_STATUS_READY;
    new_thread->pid = new_pid;
    new_thread->cpu_time = 0;

    queue_push(&ReadyQueue, new_thread); // ReadyQueue�� push

    /* Table�� update */
    pThreadTbEnt[tid].pThread = new_thread;
    pThreadTbEnt[tid].bUsed = 1;

    sem_post(SEM);

    return 0;
}

int thread_suspend(thread_t tid) {
    SEM = sem_open("mysem", O_CREAT, 0644, 1);
    sem_wait(SEM); // ���� ������ ���� ����ȭ

    // �ش�Ǵ� tid�� �����尡 �������� ����
    if (pThreadTbEnt[tid].bUsed == 0) {
        perror("thread_suspend() : That tid is not exist!");
        sem_post(SEM);
        return -1;
    }

    // Table�� tid ��Ʈ������ �������� TCB �����͸� ���´�.
    Thread *thread = pThreadTbEnt[tid].pThread;

    // �ڱ� �ڽ��� suspend ���� �ʵ��� ����
    if (pCurrentThread == thread) {
        perror("thread_suspend() : self-suspend!");
        sem_post(SEM);
        return -1;
    }

    // �ش� �����尡 Ready ������ ��� ReadyQueue���� WaitingQueue�� �̵�
    if (thread->status == THREAD_STATUS_READY) {
        queue_remove(&ReadyQueue, thread);
        thread->status = THREAD_STATUS_WAIT;
        queue_push(&WaitingQueue, thread); // Waiting Queue�� tail�� �̵�
    }

    sem_post(SEM);
    return 0;
}

int thread_cancel(thread_t tid) {
    SEM = sem_open("mysem", O_CREAT, 0644, 1);
    sem_wait(SEM); // ���� ������ ���� ����ȭ

    // �ش�Ǵ� tid�� �����尡 �������� ����
    if (pThreadTbEnt[tid].bUsed == 0) {
        perror("thread_resume() : That tid is not exist!");
        sem_post(SEM);
        return -1;
    }

    // Table�� tid ��Ʈ������ �������� TCB �����͸� ���´�.
    Thread *thread = pThreadTbEnt[tid].pThread;
    pid_t tpid = thread->pid;

    // �ڱ� �ڽ��� cancle ���� �ʵ��� ����
    if (pCurrentThread == thread) {
        perror("thread_cancel() : self-cancel!");
        sem_post(SEM);

        return -1;
    }

    kill(tpid, SIGKILL);

    /* Queue���� ���� */
    if (thread->status == THREAD_STATUS_READY) {
        queue_remove(&ReadyQueue, thread);
    } else if (thread->status == THREAD_STATUS_WAIT) {
        queue_remove(&WaitingQueue, thread);
    }

    /* Table���� �ش� ������ ���� */
    pThreadTbEnt[tid].bUsed = 0;
    pThreadTbEnt[tid].pThread = NULL;

    /* Stack �� TCB�� deallocate */
    free(thread->stackAddr);
    thread->stackAddr = NULL;
    free(thread);
    thread = NULL;

    sem_post(SEM);
    return 0;
}

int thread_resume(thread_t tid) {
    SEM = sem_open("mysem", O_CREAT, 0644, 1); // ���� ������ ���� ����ȭ
    sem_wait(SEM);

    // �ش�Ǵ� tid�� �����尡 �������� ����
    if (pThreadTbEnt[tid].bUsed == 0) {
        perror("thread_resume() : That tid is not exist!");
        sem_post(SEM);
        return -1;
    }

    // Table�� tid ��Ʈ������ �������� TCB �����͸� ���´�.
    Thread *thread = pThreadTbEnt[tid].pThread;

    /* WaitingQueue���� ReadyQueue�� �̵� */
    if (thread->status == THREAD_STATUS_WAIT) {
        queue_remove(&WaitingQueue, thread);
        thread->status = THREAD_STATUS_READY;
        queue_push(&ReadyQueue, thread); // ReadyQueue�� tail�� �̵�
    }

    sem_post(SEM);
    return 0;
}

thread_t thread_self(void) {
    SEM = sem_open("mysem", O_CREAT, 0644, 1); // ���� ������ ���� ����ȭ
    sem_wait(SEM);

    thread_t tid;

    /* Table�� ��� ��Ʈ���� ���� TCB �ּ� �� */
    for (int i = 0; i < MAX_THREAD_NUM; i++) {
        if (pThreadTbEnt[i].pThread == pCurrentThread) {
            tid = i; // ã���� tid �Ҵ�
        }
    }

    sem_post(SEM);
    return tid;
}

void disjoin(int signo) {}

int thread_join(thread_t tid) {
    SEM = sem_open("mysem", O_CREAT, 0644, 1); // ���� ������ ���� ����ȭ
    sem_wait(SEM);

    Thread *new_thread, *parent_thread, *child_thread;
    pid_t curpid, newpid;

    parent_thread = pCurrentThread; // ���� �� �Լ��� ȣ���� �����尡 �θ���
    child_thread = pThreadTbEnt[tid].pThread; // tid�� �ش��ϴ� �����尡 �ڽ���

    signal(SIGCHLD, disjoin); // SIGCHLD�� ���� �ñ׳� ���

    /* parent�� WaitingQueue�� �̵���Ų��. */
    pCurrentThread->status = THREAD_STATUS_WAIT;
    queue_push(&WaitingQueue, parent_thread);

    // ReadyQueue�� ������� ���� ��쿡�� ���ο� �����带 �����ų�� �ִ�
    if (ReadyQueue.queueCount != 0) {
        new_thread = ReadyQueue.pHead;
        queue_pop(&ReadyQueue); // ReadyQueue�� head���� �����带 �����´�

        /* ���ο� ������ TCB ������Ʈ */
        new_thread->status = THREAD_STATUS_RUN;
        new_thread->cpu_time += 2;

        newpid = new_thread->pid;
        kill(newpid, SIGCONT);       // ���ο� ������ ����
        pCurrentThread = new_thread; // pCurrentThread ������Ʈ
    } else {
        pCurrentThread = NULL; // ReadyQueue�� ����ִ� ��� �ƹ��͵� �������
    }

    sem_post(SEM); // parent�� pause ���¿� ���� ������ ����ȭ ����

    while (child_thread->status != THREAD_STATUS_ZOMBIE) {
        pause(); // SIGCHLD�� ������ ����� �ڽ��� ����Ȱ��� Ȯ��
    }

    sem_wait(SEM); // parent�� ����� �ٽ� ����ȭ�� �Ǵ�

    /* parent�� WaitingQueue���� ReadyQueue�� ������ */
    queue_remove(&WaitingQueue, parent_thread);
    queue_push(&ReadyQueue, parent_thread);
    parent_thread->status = THREAD_STATUS_READY;

    // pCurrentThread = NULL; // �ڽ��� �׾����Ƿ� ���� �������� �����尡 ������

    /* �ڽ� �������� Stack�� TCB�� deallocate */
    free(child_thread->stackAddr);
    child_thread->stackAddr = NULL;
    free(child_thread);
    child_thread = NULL;

    // Table���� child thread ����
    pThreadTbEnt[tid].bUsed = 0;
    pThreadTbEnt[tid].pThread = NULL;

    sem_post(SEM);
    kill(parent_thread->pid, SIGSTOP); // ���� �۾��� ��� ���� �� �ڽ��� ����

    return 0;
}

int thread_cputime(void) {
    SEM = sem_open("mysem", O_CREAT, 0644, 1); // ���� ������ ���� ����ȭ
    sem_wait(SEM);

    int time = (int)pCurrentThread->cpu_time;

    sem_post(SEM);
    return time;
}

void thread_exit(void) { pCurrentThread->status = THREAD_STATUS_ZOMBIE; }
