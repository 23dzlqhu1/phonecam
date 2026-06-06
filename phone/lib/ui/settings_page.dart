import 'package:flutter/material.dart';

/// 设置页面
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
      appBar: AppBar(
        title: const Text('设置'),
        centerTitle: true,
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          // 分辨率
          _buildSection(
            title: '分辨率',
            icon: Icons.aspect_ratio,
            child: _buildChoiceChip(
              options: ['320x240', '640x480', '1280x720'],
              selected: _resolution,
              onSelected: (v) => setState(() => _resolution = v),
            ),
          ),

          const SizedBox(height: 16),

          // 帧率
          _buildSection(
            title: '帧率',
            icon: Icons.speed,
            child: _buildChoiceChip(
              options: ['10', '15', '24', '30'],
              selected: '$_fps',
              labelBuilder: (v) => '$v fps',
              onSelected: (v) => setState(() => _fps = int.parse(v)),
            ),
          ),

          const SizedBox(height: 16),

          // JPEG 质量
          _buildSection(
            title: 'JPEG 质量',
            icon: Icons.high_quality,
            child: Column(
              children: [
                Slider(
                  value: _jpegQuality.toDouble(),
                  min: 50,
                  max: 95,
                  divisions: 9,
                  label: '$_jpegQuality',
                  onChanged: (v) => setState(() => _jpegQuality = v.round()),
                ),
                Text(
                  '$_jpegQuality （质量越高，带宽需求越大）',
                  style: Theme.of(context).textTheme.bodySmall?.copyWith(
                    color: Colors.grey,
                  ),
                ),
              ],
            ),
          ),

          const SizedBox(height: 16),

          // 端口
          _buildSection(
            title: 'HTTP 端口',
            icon: Icons.language,
            child: TextField(
              controller: TextEditingController(text: '$_port'),
              keyboardType: TextInputType.number,
              decoration: InputDecoration(
                border: const OutlineInputBorder(),
                hintText: '默认 8080',
                suffixText: '端口',
                contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
              ),
              onChanged: (v) {
                final p = int.tryParse(v);
                if (p != null && p > 0 && p < 65536) {
                  setState(() => _port = p);
                }
              },
            ),
          ),

          const SizedBox(height: 32),

          // 关于
          Container(
            padding: const EdgeInsets.all(16),
            decoration: BoxDecoration(
              color: Colors.grey.shade900,
              borderRadius: BorderRadius.circular(12),
            ),
            child: Column(
              children: [
                const Text(
                  'PhoneCam',
                  style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold),
                ),
                const SizedBox(height: 4),
                Text(
                  '手机摄像头救急工具 v0.3.0',
                  style: TextStyle(color: Colors.grey.shade500, fontSize: 13),
                ),
                const SizedBox(height: 8),
                Text(
                  'MIT License · 2026',
                  style: TextStyle(color: Colors.grey.shade600, fontSize: 12),
                ),
              ],
            ),
          ),
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
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Colors.grey.shade900,
        borderRadius: BorderRadius.circular(12),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(icon, size: 20, color: Colors.blue.shade300),
              const SizedBox(width: 8),
              Text(title, style: const TextStyle(fontWeight: FontWeight.w600)),
            ],
          ),
          const SizedBox(height: 12),
          child,
        ],
      ),
    );
  }

  Widget _buildChoiceChip({
    required List<String> options,
    required String selected,
    String Function(String)? labelBuilder,
    required ValueChanged<String> onSelected,
  }) {
    return Wrap(
      spacing: 8,
      children: options.map((option) {
        final isSelected = option == selected;
        return ChoiceChip(
          label: Text(labelBuilder != null ? labelBuilder(option) : option),
          selected: isSelected,
          onSelected: (_) => onSelected(option),
          selectedColor: Colors.blue.shade700,
        );
      }).toList(),
    );
  }
}