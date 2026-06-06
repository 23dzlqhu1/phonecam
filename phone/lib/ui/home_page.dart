import 'package:flutter/material.dart';
import 'settings_page.dart';

/// 主页面
class HomePage extends StatefulWidget {
  final VoidCallback onStartStreaming;
  final VoidCallback onStopStreaming;
  final bool isStreaming;
  final bool isCameraReady;
  final String statusText;
  final String? serverUrl;
  final int clientCount;

  const HomePage({
    super.key,
    required this.onStartStreaming,
    required this.onStopStreaming,
    required this.isStreaming,
    required this.isCameraReady,
    required this.statusText,
    this.serverUrl,
    this.clientCount = 0,
  });

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> with SingleTickerProviderStateMixin {
  late AnimationController _pulseController;

  @override
  void initState() {
    super.initState();
    _pulseController = AnimationController(
      vsync: this,
      duration: const Duration(seconds: 1),
    )..repeat(reverse: true);
  }

  @override
  void dispose() {
    _pulseController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('PhoneCam'),
        centerTitle: true,
        actions: [
          IconButton(
            icon: const Icon(Icons.settings),
            onPressed: () {
              Navigator.push(
                context,
                MaterialPageRoute(builder: (_) => const SettingsPage()),
              );
            },
          ),
        ],
      ),
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 24),
          child: Column(
            children: [
              const Spacer(flex: 1),

              // 推流状态指示
              _buildStatusIndicator(),

              const SizedBox(height: 32),

              // 主按钮
              _buildStreamButton(),

              const SizedBox(height: 24),

              // 服务器地址
              if (widget.serverUrl != null) _buildUrlCard(),

              const SizedBox(height: 16),

              // 连接数
              if (widget.isStreaming)
                Text(
                  '连接设备: ${widget.clientCount}',
                  style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                    color: Colors.grey,
                  ),
                ),

              const Spacer(flex: 2),

              // 底部提示
              _buildTip(),

              const SizedBox(height: 16),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildStatusIndicator() {
    final isStreaming = widget.isStreaming;
    return Column(
      children: [
        // 动画圆圈
        AnimatedBuilder(
          animation: _pulseController,
          builder: (context, child) {
            final scale = isStreaming ? 1.0 + _pulseController.value * 0.05 : 1.0;
            return Transform.scale(
              scale: scale,
              child: Container(
                width: 120,
                height: 120,
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  color: isStreaming
                      ? Colors.red.withOpacity(0.15)
                      : Colors.grey.withOpacity(0.1),
                  border: Border.all(
                    color: isStreaming ? Colors.red : Colors.grey.shade600,
                    width: 3,
                  ),
                ),
                child: Icon(
                  isStreaming ? Icons.videocam : Icons.videocam_off,
                  size: 48,
                  color: isStreaming ? Colors.red : Colors.grey,
                ),
              ),
            );
          },
        ),
        const SizedBox(height: 16),
        Text(
          widget.statusText,
          style: Theme.of(context).textTheme.titleMedium?.copyWith(
            color: isStreaming ? Colors.red : Colors.grey,
            fontWeight: FontWeight.w500,
          ),
        ),
      ],
    );
  }

  Widget _buildStreamButton() {
    return SizedBox(
      width: double.infinity,
      height: 56,
      child: FilledButton.icon(
        onPressed: widget.isCameraReady
            ? (widget.isStreaming ? widget.onStopStreaming : widget.onStartStreaming)
            : null,
        icon: Icon(widget.isStreaming ? Icons.stop : Icons.play_arrow, size: 28),
        label: Text(
          widget.isStreaming ? '停止推流' : '开始推流',
          style: const TextStyle(fontSize: 18),
        ),
        style: FilledButton.styleFrom(
          backgroundColor: widget.isStreaming ? Colors.red : null,
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
        ),
      ),
    );
  }

  Widget _buildUrlCard() {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 14),
      decoration: BoxDecoration(
        color: Colors.grey.shade900,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: Colors.greenAccent.withOpacity(0.3)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text(
            '推流地址',
            style: TextStyle(color: Colors.grey, fontSize: 12),
          ),
          const SizedBox(height: 4),
          Text(
            widget.serverUrl!,
            style: const TextStyle(
              fontFamily: 'monospace',
              fontSize: 14,
              color: Colors.greenAccent,
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildTip() {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Colors.blue.withOpacity(0.1),
        borderRadius: BorderRadius.circular(12),
      ),
      child: Row(
        children: [
          Icon(Icons.info_outline, color: Colors.blue.shade300, size: 20),
          const SizedBox(width: 12),
          Expanded(
            child: Text(
              '电脑端运行 python phonecam.py 即可自动连接',
              style: TextStyle(color: Colors.blue.shade200, fontSize: 13),
            ),
          ),
        ],
      ),
    );
  }
}