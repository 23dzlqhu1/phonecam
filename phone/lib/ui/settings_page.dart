import 'package:flutter/material.dart';

/// 设置页面 - 简洁现代设计
class SettingsPage extends StatefulWidget {
  const SettingsPage({super.key});

  @override
  State<SettingsPage> createState() => _SettingsPageState();
}

class _SettingsPageState extends State<SettingsPage> {
  String _resolution = '640x480';
  int _fps = 15;
  int _jpegQuality = 80;
  int _port = 8080;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF0A0A0F),
      appBar: AppBar(
        backgroundColor: Colors.transparent,
        elevation: 0,
        title: const Text('设置'),
        centerTitle: true,
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          _buildSection(
            title: '视频质量',
            icon: Icons.high_quality_rounded,
            child: Column(
              children: [
                _buildSettingRow(
                  label: '分辨率',
                  child: _buildDropdown(
                    value: _resolution,
                    items: ['320x240', '640x480', '1280x720'],
                    onChanged: (v) => setState(() => _resolution = v!),
                  ),
                ),
                const Divider(color: Color(0xFF1F2937), height: 1),
                _buildSettingRow(
                  label: '帧率',
                  child: _buildDropdown(
                    value: '$_fps',
                    items: ['10', '15', '24', '30'],
                    labelBuilder: (v) => '$v fps',
                    onChanged: (v) => setState(() => _fps = int.parse(v!)),
                  ),
                ),
                const Divider(color: Color(0xFF1F2937), height: 1),
                _buildSettingRow(
                  label: 'JPEG 质量',
                  child: Row(
                    children: [
                      Expanded(
                        child: SliderTheme(
                          data: SliderTheme.of(context).copyWith(
                            activeTrackColor: const Color(0xFF3B82F6),
                            inactiveTrackColor: const Color(0xFF1F2937),
                            thumbColor: const Color(0xFF3B82F6),
                            overlayColor: const Color(0xFF3B82F6).withOpacity(0.2),
                          ),
                          child: Slider(
                            value: _jpegQuality.toDouble(),
                            min: 50,
                            max: 95,
                            divisions: 9,
                            onChanged: (v) => setState(() => _jpegQuality = v.round()),
                          ),
                        ),
                      ),
                      SizedBox(
                        width: 40,
                        child: Text(
                          '$_jpegQuality',
                          textAlign: TextAlign.center,
                          style: const TextStyle(
                            color: Color(0xFF9CA3AF),
                            fontWeight: FontWeight.w500,
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 16),
          _buildSection(
            title: '网络',
            icon: Icons.language_rounded,
            child: _buildSettingRow(
              label: 'HTTP 端口',
              child: SizedBox(
                width: 100,
                child: TextField(
                  controller: TextEditingController(text: '$_port'),
                  keyboardType: TextInputType.number,
                  textAlign: TextAlign.center,
                  style: const TextStyle(color: Color(0xFFE5E7EB)),
                  decoration: InputDecoration(
                    contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                    border: OutlineInputBorder(
                      borderRadius: BorderRadius.circular(8),
                      borderSide: const BorderSide(color: Color(0xFF374151)),
                    ),
                    enabledBorder: OutlineInputBorder(
                      borderRadius: BorderRadius.circular(8),
                      borderSide: const BorderSide(color: Color(0xFF374151)),
                    ),
                    focusedBorder: OutlineInputBorder(
                      borderRadius: BorderRadius.circular(8),
                      borderSide: const BorderSide(color: Color(0xFF3B82F6)),
                    ),
                  ),
                  onChanged: (v) {
                    final p = int.tryParse(v);
                    if (p != null && p > 0 && p < 65536) {
                      setState(() => _port = p);
                    }
                  },
                ),
              ),
            ),
          ),
          const SizedBox(height: 32),
          _buildAboutSection(),
        ],
      ),
    );
  }

  Widget _buildSection({
    required String title,
    required IconData icon,
    required Widget child,
  }) {
    return Container(
      decoration: BoxDecoration(
        color: const Color(0xFF111827),
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: const Color(0xFF1F2937)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 16, 16, 12),
            child: Row(
              children: [
                Icon(icon, size: 18, color: const Color(0xFF3B82F6)),
                const SizedBox(width: 10),
                Text(
                  title,
                  style: const TextStyle(
                    fontWeight: FontWeight.w600,
                    fontSize: 15,
                  ),
                ),
              ],
            ),
          ),
          child,
        ],
      ),
    );
  }

  Widget _buildSettingRow({required String label, required Widget child}) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(
            label,
            style: const TextStyle(
              color: Color(0xFF9CA3AF),
              fontSize: 14,
            ),
          ),
          child,
        ],
      ),
    );
  }

  Widget _buildDropdown({
    required String value,
    required List<String> items,
    String Function(String)? labelBuilder,
    required ValueChanged<String?> onChanged,
  }) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12),
      decoration: BoxDecoration(
        color: const Color(0xFF1F2937),
        borderRadius: BorderRadius.circular(8),
      ),
      child: DropdownButton<String>(
        value: value,
        items: items.map((item) {
          return DropdownMenuItem(
            value: item,
            child: Text(
              labelBuilder != null ? labelBuilder(item) : item,
              style: const TextStyle(color: Color(0xFFE5E7EB)),
            ),
          );
        }).toList(),
        onChanged: onChanged,
        underline: const SizedBox(),
        dropdownColor: const Color(0xFF1F2937),
        icon: const Icon(Icons.arrow_drop_down_rounded, color: Color(0xFF6B7280)),
      ),
    );
  }

  Widget _buildAboutSection() {
    return Container(
      padding: const EdgeInsets.all(24),
      decoration: BoxDecoration(
        color: const Color(0xFF111827),
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: const Color(0xFF1F2937)),
      ),
      child: Column(
        children: [
          Container(
            width: 64,
            height: 64,
            decoration: BoxDecoration(
              color: const Color(0xFF1F2937),
              borderRadius: BorderRadius.circular(16),
            ),
            child: const Icon(
              Icons.videocam_rounded,
              size: 32,
              color: Color(0xFF3B82F6),
            ),
          ),
          const SizedBox(height: 16),
          const Text(
            'PhoneCam',
            style: TextStyle(
              fontSize: 20,
              fontWeight: FontWeight.w700,
              letterSpacing: 1.2,
            ),
          ),
          const SizedBox(height: 4),
          const Text(
            '手机摄像头救急工具',
            style: TextStyle(
              color: Color(0xFF6B7280),
              fontSize: 14,
            ),
          ),
          const SizedBox(height: 8),
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
            decoration: BoxDecoration(
              color: const Color(0xFF1F2937),
              borderRadius: BorderRadius.circular(12),
            ),
            child: const Text(
              'v0.4.0',
              style: TextStyle(
                color: Color(0xFF9CA3AF),
                fontSize: 12,
                fontWeight: FontWeight.w500,
              ),
            ),
          ),
          const SizedBox(height: 16),
          const Text(
            'MIT License · 2026',
            style: TextStyle(
              color: Color(0xFF4B5563),
              fontSize: 12,
            ),
          ),
        ],
      ),
    );
  }
}