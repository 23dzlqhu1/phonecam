package com.phonecam.phone

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.os.Handler
import android.os.Looper
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import java.nio.ByteBuffer

class H264EncoderPlugin : FlutterPlugin, MethodChannel.MethodCallHandler {
    private lateinit var channel: MethodChannel
    private var encoder: MediaCodec? = null
    private var isInitialized = false
    private var frameIndex = 0L
    private var pendingResult: MethodChannel.Result? = null

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        channel = MethodChannel(binding.binaryMessenger, "com.phonecam/h264")
        channel.setMethodCallHandler(this)
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        channel.setMethodCallHandler(null)
        release()
    }

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        when (call.method) {
            "init" -> {
                val width = call.argument<Int>("width") ?: 640
                val height = call.argument<Int>("height") ?: 480
                val fps = call.argument<Int>("fps") ?: 30
                val bitrate = call.argument<Int>("bitrate") ?: 1_000_000
                initEncoder(width, height, fps, bitrate, result)
            }
            "encode" -> {
                val data = call.argument<ByteArray>("data")
                if (data != null) {
                    encode(data, result)
                } else {
                    result.error("INVALID_ARGS", "No data provided", null)
                }
            }
            "requestKeyframe" -> {
                requestKeyframe(result)
            }
            "release" -> {
                release()
                result.success(null)
            }
            else -> result.notImplemented()
        }
    }

    private fun initEncoder(width: Int, height: Int, fps: Int, bitrate: Int, result: MethodChannel.Result) {
        try {
            release()

            val format = MediaFormat.createVideoFormat(
                MediaFormat.MIMETYPE_VIDEO_AVC, width, height
            ).apply {
                setInteger(MediaFormat.KEY_BIT_RATE, bitrate)
                setInteger(MediaFormat.KEY_FRAME_RATE, fps)
                setInteger(MediaFormat.KEY_COLOR_FORMAT,
                    MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible)
                setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 2) // 每2秒一个IDR
                // Baseline Profile, Level 3.1
                setInteger(MediaFormat.KEY_PROFILE,
                    MediaCodecInfo.CodecProfileLevel.AVCProfileBaseline)
                setInteger(MediaFormat.KEY_LEVEL,
                    MediaCodecInfo.CodecProfileLevel.AVCLevel31)
                // 低延迟
                setInteger(MediaFormat.KEY_LATENCY, 0)
            }

            encoder = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
            encoder?.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            encoder?.start()

            isInitialized = true
            frameIndex = 0
            result.success(mapOf(
                "status" to "ok",
                "width" to width,
                "height" to height,
                "fps" to fps,
                "bitrate" to bitrate
            ))
        } catch (e: Exception) {
            result.error("INIT_FAILED", e.message, null)
        }
    }

    private fun encode(yuvData: ByteArray, result: MethodChannel.Result) {
        if (!isInitialized || encoder == null) {
            result.error("NOT_INITIALIZED", "Encoder not initialized", null)
            return
        }

        try {
            val enc = encoder!!

            // 输入
            val inputIndex = enc.dequeueInputBuffer(0)
            if (inputIndex >= 0) {
                val inputBuffer = enc.getInputBuffer(inputIndex)
                inputBuffer?.clear()
                inputBuffer?.put(yuvData)
                val pts = frameIndex * 1_000_000L / 30 // 假设30fps
                enc.queueInputBuffer(inputIndex, 0, yuvData.size, pts, 0)
                frameIndex++
            }

            // 输出
            val bufferInfo = MediaCodec.BufferInfo()
            val outputIndex = enc.dequeueOutputBuffer(bufferInfo, 0)
            if (outputIndex >= 0) {
                val outputBuffer = enc.getOutputBuffer(outputIndex)
                val nalData = ByteArray(bufferInfo.size)
                outputBuffer?.get(nalData)
                enc.releaseOutputBuffer(outputIndex, false)

                val isKeyFrame = (bufferInfo.flags and MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0
                result.success(mapOf(
                    "data" to nalData,
                    "pts" to bufferInfo.presentationTimeUs,
                    "keyframe" to isKeyFrame,
                    "size" to nalData.size
                ))
            } else {
                // 没有输出帧（可能还在缓冲）
                result.success(null)
            }
        } catch (e: Exception) {
            result.error("ENCODE_FAILED", e.message, null)
        }
    }

    private fun requestKeyframe(result: MethodChannel.Result) {
        try {
            // 通过设置 IDR 请求参数
            val params = android.os.Bundle()
            params.putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0)
            encoder?.setParameters(params)
            result.success(true)
        } catch (e: Exception) {
            result.error("KEYFRAME_FAILED", e.message, null)
        }
    }

    private fun release() {
        try {
            encoder?.stop()
            encoder?.release()
        } catch (_: Exception) {}
        encoder = null
        isInitialized = false
    }
}