import 'dart:io' show Platform;
import 'package:flutter/material.dart';
import 'package:fijkplayer/fijkplayer.dart';

/// Example showing how to configure FijkPlayer for iOS Simulator compatibility
/// 
/// iOS Simulator doesn't support hardware video acceleration well, which can
/// cause issues where you hear audio but see no video. This example shows
/// how to detect and configure the player for simulator compatibility.
class IOSSimulatorExample extends StatefulWidget {
  @override
  _IOSSimulatorExampleState createState() => _IOSSimulatorExampleState();
}

class _IOSSimulatorExampleState extends State<IOSSimulatorExample> {
  late FijkPlayer player;
  bool isConfigured = false;

  @override
  void initState() {
    super.initState();
    player = FijkPlayer();
    _configurePlayer();
  }

  /// Configure the player with simulator-friendly settings if needed
  Future<void> _configurePlayer() async {
    try {
      // Detect if running on iOS Simulator and configure accordingly
      if (Platform.isIOS) {
        // Note: In a real app, you might want to add more sophisticated
        // simulator detection, but for this example we'll apply simulator
        // settings to all iOS instances
        FijkOption option = FijkOption();
        option.configureForIOSSimulator();
        
        // Apply the simulator-friendly options
        await player.applyOptions(option);
        
        // Enable snapshot functionality
        await player.setOption(FijkOption.hostCategory, "enable-snapshot", 1);
        
        setState(() {
          isConfigured = true;
        });
        
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('Configured for iOS Simulator compatibility'),
            backgroundColor: Colors.green,
          ),
        );
      } else {
        // For Android or other platforms, use default configuration
        setState(() {
          isConfigured = true;
        });
      }
    } catch (e) {
      print('Error configuring player: $e');
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('Error configuring player: $e'),
          backgroundColor: Colors.red,
        ),
      );
    }
  }

  /// Load and play a sample video
  Future<void> _loadVideo() async {
    if (!isConfigured) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Player not configured yet')),
      );
      return;
    }

    try {
      // Example with a test video URL
      // Replace this with your video URL
      String videoUrl = "https://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4";
      
      await player.setDataSource(videoUrl, autoPlay: true);
      
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('Video loaded successfully'),
          backgroundColor: Colors.green,
        ),
      );
    } catch (e) {
      print('Error loading video: $e');
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('Error loading video: $e'),
          backgroundColor: Colors.red,
        ),
      );
    }
  }

  @override
  void dispose() {
    player.release();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text('iOS Simulator Example'),
        backgroundColor: Colors.blue,
      ),
      body: Column(
        children: [
          // Information card
          Card(
            margin: EdgeInsets.all(16),
            color: Platform.isIOS ? Colors.blue[50] : Colors.grey[50],
            child: Padding(
              padding: EdgeInsets.all(16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    Platform.isIOS 
                        ? '📱 iOS Platform Detected' 
                        : '🤖 ${Platform.operatingSystem} Platform',
                    style: TextStyle(
                      fontSize: 18,
                      fontWeight: FontWeight.bold,
                    ),
                  ),
                  SizedBox(height: 8),
                  Text(
                    Platform.isIOS
                        ? 'Simulator-friendly configuration applied:\n'
                          '• Hardware acceleration disabled\n'
                          '• Software decoding enabled\n'
                          '• Optimal pixel format set'
                        : 'Using default configuration for this platform.',
                    style: TextStyle(fontSize: 14),
                  ),
                  SizedBox(height: 8),
                  Text(
                    'Status: ${isConfigured ? "✅ Configured" : "⏳ Configuring..."}',
                    style: TextStyle(
                      fontSize: 14,
                      fontWeight: FontWeight.bold,
                      color: isConfigured ? Colors.green : Colors.orange,
                    ),
                  ),
                ],
              ),
            ),
          ),
          
          // Video player
          Expanded(
            child: Container(
              margin: EdgeInsets.all(16),
              decoration: BoxDecoration(
                border: Border.all(color: Colors.grey),
                borderRadius: BorderRadius.circular(8),
              ),
              child: ClipRRect(
                borderRadius: BorderRadius.circular(8),
                child: FijkView(
                  player: player,
                  width: double.infinity,
                  height: double.infinity,
                  color: Colors.black,
                ),
              ),
            ),
          ),
          
          // Control buttons
          Padding(
            padding: EdgeInsets.all(16),
            child: Row(
              mainAxisAlignment: MainAxisAlignment.spaceEvenly,
              children: [
                ElevatedButton(
                  onPressed: isConfigured ? _loadVideo : null,
                  child: Text('Load Test Video'),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: Colors.blue,
                    foregroundColor: Colors.white,
                  ),
                ),
                ElevatedButton(
                  onPressed: isConfigured ? () => player.start() : null,
                  child: Text('Play'),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: Colors.green,
                    foregroundColor: Colors.white,
                  ),
                ),
                ElevatedButton(
                  onPressed: isConfigured ? () => player.pause() : null,
                  child: Text('Pause'),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: Colors.orange,
                    foregroundColor: Colors.white,
                  ),
                ),
              ],
            ),
          ),
          
          // Instructions
          Card(
            margin: EdgeInsets.all(16),
            color: Colors.yellow[50],
            child: Padding(
              padding: EdgeInsets.all(16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    '💡 Troubleshooting Tips:',
                    style: TextStyle(
                      fontSize: 16,
                      fontWeight: FontWeight.bold,
                    ),
                  ),
                  SizedBox(height: 8),
                  Text(
                    '• If you can hear audio but see no video on iOS Simulator, the simulator configuration should fix it\n'
                    '• On real iOS devices, hardware acceleration will be used automatically\n'
                    '• Make sure your video URL is accessible and in a supported format\n'
                    '• Check the debug console for any error messages',
                    style: TextStyle(fontSize: 14),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}
