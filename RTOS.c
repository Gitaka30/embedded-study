#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

// センサーデータを格納する構造体
typedef struct {
    unsigned short distance; // 距離データ
    unsigned short angle;    // 角度データ
} LidarData_t;

// 模擬用のグローバル変数（物理メモリの代わり。セグフォが起きない）
unsigned int mock_lidar_ctrl_reg = 0;
#define LASER_ON_BIT      (1 << 0)

// スレッド間で安全にデータを渡すためのバッファ（簡易キュー）
LidarData_t shared_queue[10];
int queue_count = 0;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

/* ----------------------------------------------------
 * 1. レーザー制御・データ生成タスク (FreeRTOSのvLaserControlTaskに相当)
 * -------------------------------------------------- */
void* vLaserControlTask(void *arg) {
    unsigned short current_angle = 0;
    for (;;) {
        // ハードウェア制御の模擬
        mock_lidar_ctrl_reg |= LASER_ON_BIT;
        
        // 模擬データを作成（障害物がだんだん近づいてくるシミュレーション）
        LidarData_t data;
        data.angle = current_angle;
        
        static int dist = 800;
        dist -= 40;
        if (dist < 200) dist = 800; // 200mm〜800mmの間をループ
        data.distance = dist;

        current_angle = (current_angle + 1) % 360;

        // キュー（共有バッファ）にデータを送信
        pthread_mutex_lock(&queue_mutex);
        if (queue_count < 10) {
            shared_queue[queue_count] = data;
            queue_count++;
            // 受信タスクに「データが入ったぞ！」とシグナルを送って起こす
            pthread_cond_signal(&queue_cond);
        }
        pthread_mutex_unlock(&queue_mutex);

        // 100ms待機（PCの画面出力スピードに合わせて調整）
        usleep(100000); 
    }
    return NULL;
}

/* ----------------------------------------------------
 * 2. データ受信・処理タスク (FreeRTOSのvDataReceiveTaskに相当)
 * -------------------------------------------------- */
void* vDataReceiveTask(void *arg) {
    LidarData_t receivedData;

    for (;;) {
        pthread_mutex_lock(&queue_mutex);
        // データが届くまで待機（条件変数のウェイト：待っている間はCPUを消費しない）
        while (queue_count == 0) {
            pthread_cond_wait(&queue_cond, &queue_mutex);
        }
        
        // キューからデータを取り出す
        queue_count--;
        receivedData = shared_queue[queue_count];
        pthread_mutex_unlock(&queue_mutex);

        // 障害物検知のロジック
        if (receivedData.distance < 500) { // 50cm以内
            printf("[ALERT] Obstacle detected at %d mm! (Angle: %d deg)\n", 
                   receivedData.distance, receivedData.angle);
        } else {
            printf("Safe: Distance %d mm\n", receivedData.distance);
        }
    }
    return NULL;
}

/* ----------------------------------------------------
 * メイン関数（スレッドの起動）
 * -------------------------------------------------- */
int main(void) {
    pthread_t thread_laser, thread_recv;

    printf("Starting PC Lidar Simulator...\n");

    // Linuxの標準機能で2つのタスク（スレッド）を同時に走らせる
    pthread_create(&thread_laser, NULL, vLaserControlTask, NULL);
    pthread_create(&thread_recv, NULL, vDataReceiveTask, NULL);

    // スレッドが動いている間、メイン関数を維持する
    pthread_join(thread_laser, NULL);
    pthread_join(thread_recv, NULL);

    return 0;
}
