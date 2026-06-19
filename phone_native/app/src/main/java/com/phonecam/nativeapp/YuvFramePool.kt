package com.phonecam.nativeapp

import android.util.Log
import java.util.concurrent.atomic.AtomicInteger

/**
 * YUV 帧缓冲池 — 解决 OOM 问题
 *
 * 问题：每帧分配 ~2MB ByteArray (1080p YUV420)，30fps 下 GC/OOM。
 * 方案：预分配固定大小 buffer 池，acquire/release 模式。
 *
 * 关键设计：
 * - pool size 3-4，够双缓冲 + pipeline 延迟
 * - acquire 失败直接丢帧，不阻塞 ImageReaderThread
 * - release 只能在 EGL 线程完成 drawYuv 后调用
 * - 分辨率变化时重建 pool
 */
class YuvFramePool(initialWidth: Int = 1280, initialHeight: Int = 720) {
    companion object {
        private const val TAG = "YuvFramePool"
        private const val POOL_SIZE = 4  // 4 buffers: enough for double-buffer + pipeline
    }

    // Buffer wrapper with ownership
    class YuvFrameBuffer(
        val data: ByteArray,
        val width: Int,
        val height: Int,
        private val pool: YuvFramePool
    ) {
        @Volatile var ptsNs: Long = 0L
        @Volatile var rotation: Int = 0

        fun release() {
            pool.release(this)
        }
    }

    private val buffers = ArrayDeque<YuvFrameBuffer>(POOL_SIZE)
    private val lock = Any()

    @Volatile private var currentWidth = initialWidth
    @Volatile private var currentHeight = initialHeight
    private val bufferSize get() = currentWidth * currentHeight * 3 / 2

    // Diagnostics
    val availableCount: AtomicInteger = AtomicInteger(POOL_SIZE)
    val inUseCount: AtomicInteger = AtomicInteger(0)
    val droppedFrames: AtomicInteger = AtomicInteger(0)
    val poolResets: AtomicInteger = AtomicInteger(0)

    init {
        // Pre-allocate all buffers
        synchronized(lock) {
            for (i in 0 until POOL_SIZE) {
                buffers.add(YuvFrameBuffer(ByteArray(bufferSize), currentWidth, currentHeight, this))
            }
        }
        Log.i(TAG, "Pool initialized: ${POOL_SIZE} buffers, ${currentWidth}x${currentHeight}, ${bufferSize} bytes each")
    }

    /**
     * Acquire a buffer for writing. Returns null if no buffer available (drop frame).
     * MUST NOT block — called from ImageReaderThread.
     */
    fun acquire(width: Int, height: Int): YuvFrameBuffer? {
        // Check if resolution changed
        if (width != currentWidth || height != currentHeight) {
            Log.i(TAG, "Resolution changed: ${currentWidth}x${currentHeight} -> ${width}x${height}, resetting pool")
            resetPool(width, height)
        }

        synchronized(lock) {
            val buffer = buffers.removeFirstOrNull()
            if (buffer != null) {
                availableCount.decrementAndGet()
                inUseCount.incrementAndGet()
                return buffer
            }
        }

        // No buffer available — drop frame
        droppedFrames.incrementAndGet()
        return null
    }

    /**
     * Release buffer back to pool. Called from EGL thread after drawYuv.
     */
    private fun release(buffer: YuvFrameBuffer) {
        synchronized(lock) {
            buffers.addLast(buffer)
            availableCount.incrementAndGet()
            inUseCount.decrementAndGet()
        }
    }

    /**
     * Reset pool on resolution change. Old buffers become garbage.
     */
    private fun resetPool(newWidth: Int, newHeight: Int) {
        synchronized(lock) {
            buffers.clear()
            currentWidth = newWidth
            currentHeight = newHeight
            val size = bufferSize
            for (i in 0 until POOL_SIZE) {
                buffers.add(YuvFrameBuffer(ByteArray(size), newWidth, newHeight, this))
            }
            availableCount.set(POOL_SIZE)
            inUseCount.set(0)
            poolResets.incrementAndGet()
            Log.i(TAG, "Pool reset: ${POOL_SIZE} buffers, ${newWidth}x${newHeight}, ${size} bytes each")
        }
    }

    fun getDiagnostics(): String {
        return "pool=${availableCount.get()}/${POOL_SIZE} inUse=${inUseCount.get()} dropped=${droppedFrames.get()} resets=${poolResets.get()}"
    }
}
