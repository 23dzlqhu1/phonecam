import 'package:flutter/material.dart';
import 'settings_page.dart';

/// 主页面 - 简洁现代设计
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
      duration: const Duration(seconds: 2),
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
      backgroundColor: const Color(0xFF0A0A0F),
      appBar: AppBar(
        backgroundColor: Colors.transparent,
        elevation: 0,
        title: const Text(
          'PhoneCam',
          style: TextStyle(
            fontWeight: FontWeight.w600,
            letterSpacing: 1.2,
          ),
        ),
        centerTitle: true,
        actions: [
          IconButton(
            icon: const Icon(Icons.settings_outlined),
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
              _buildStatusIndicator(),
              const SizedBox(height: 40),
              _buildStreamButton(),
              const SizedBox(height: 24),
              if (widget.serverUrl != null) _buildUrlCard(),
              const SizedBox(height: 16),
              if (widget.isStreaming)
                Text(
                  '${widget.clientCount} 台设备已连接',
                  style: const TextStyle(
                    color: Color(0xFF6B7280),
                    fontSize: 14,
                  ),
                ),
              const Spacer(flex: 2),
              _buildTip(),
              const SizedBox(height: 24),
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
        AnimatedBuilder(
          animation: _pulseController,
          builder: (context, child) {
            final opacity = isStreaming
                ? 0.3 + _pulseController.value * 0.2
                : 0.1;
            return Container(
              width: 140,
              height: 140,
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                gradient: RadialGradient(
                  colors: [
                    (isStreaming ? Colors.red : Colors.grey).withOpacity(opacity),
                    Colors.transparent,
                  ],
                ),
                border: Border.all(
                  color: isStreaming
                      ? const Color(0xFFEF4444)
                      : const Color(0xFF374151),
                  width: 2,
                ),
              ),
              child: Center(
                child: Container(
                  width: 100,
                  height: 100,
                  decoration: BoxDecoration(
                    shape: BoxShape.circle,
                    color: isStreaming
                        ? const Color(0xFFEF4444).withOpacity(0.15)
                        : const Color(0xFF1F2937),
                  ),
                  child: Icon(
                    isStreaming ? Icons.videocam_rounded : Icons.videocam_off_rounded,
                    size: 44,
                    color: isStreaming
                        ? const Color(0xFFEF4444)
                        : const Color(0xFF6B7280),
                  ),
                ),
              ),
            );
          },
        ),
        const SizedBox(height: 20),
        Text(
          widget.statusText,
          style: TextStyle(
            fontSize: 16,
            fontWeight: FontWeight.w500,
            color: isStreaming
                ? const Color(0xFFEF4444)
                : const Color(0xFF9CA3AF),
          ),
        ),
      ],
    );
  }

  Widget _buildStreamButton() {
    final isStreaming = widget.isStreaming;
    return SizedBox(
      width: double.infinity,
      height: 56,
      child: ElevatedButton(
        onPressed: widget.isCameraReady
            ? (isStreaming ? widget.onStopStreaming : widget.onStartStreaming)
            : null,
        style: ElevatedButton.styleFrom(
          backgroundColor: isStreaming
              ? const Color(0xFF1F2937)
              : const Color(0xFF3B82F6),
          foregroundColor: Colors.white,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(16),
            side: isStreaming
                ? const BorderSide(color: Color(0xFF374151))
                : BorderSide.none,
          ),
          elevation: 0,
        ),
        child: Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(
              isStreaming ? Icons.stop_rounded : Icons.play_arrow_rounded,
              size: 24,
            ),
            const SizedBox(width: 8),
            Text(
              isStreaming ? '停止推流' : '开始推流',
              style: const TextStyle(
                fontSize: 16,
                fontWeight: FontWeight.w600,
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildUrlCard() {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: const Color(0xFF111827),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: const Color(0xFF1F2937)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Row(
            children: [
              Icon(Icons.link_rounded, size: 16, color: Color(0xFF6B7280)),
              SizedBox(width: 8),
              Text(
                '推流地址',
                style: TextStyle(
                  color: Color(0xFF6B7280),
                  fontSize: 12,
                  fontWeight: FontWeight.w500,
                ),
              ),
            ],
          ),
          const SizedBox(height: 8),
          Text(
            widget.serverUrl!,
            style: const TextStyle(
              fontFamily: 'JetBrains Mono',
              fontSize: 14,
              color: Color(0xFF10B981),
              fontWeight: FontWeight.w500,
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
        color: const Color(0xFF111827),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: const Color(0xFF1F2937)),
      ),
      child: const Row(
        children: [
          Icon(Icons.info_outline_rounded, color: Color(0xFF3B82F6), size: 20),
          SizedBox(width: 12),
          Expanded(
            child: Text(
              '电脑端运行 python phonecam.py 即可自动连接',
              style: TextStyle(
                color: Color(0xFF9CA3AF),
                fontSize: 13,
              ),
            ),
          ),
        ],
      ),
    );
  }
}