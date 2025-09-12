import 'package:flutter/material.dart';
import 'package:fijkplayer/fijkplayer.dart';
import 'dart:io';

/// iOS Simulator Diagnostic Widget
/// Copy this to your MicVision project to test iOS Simulator detection
class IOSSimulatorDiagnostic extends StatefulWidget {
  @override
  _IOSSimulatorDiagnosticState createState() => _IOSSimulatorDiagnosticState();
}

class _IOSSimulatorDiagnosticState extends State<IOSSimulatorDiagnostic> {
  final FijkPlayer player = FijkPlayer();
  String status = "Ready";
  bool isPlaying = false;

  // Test URL that works
  final String testUrl = "https://test-videos.co.uk/vids/bigbuckbunny/mp4/h264/1080/Big_Buck_Bunny_1080_10s_1MB.mp4";

  @override
  void initState() {
    super.initState();
    setState(() {
      status = Platform.isIOS 
        ? "iOS Platform - Check console for 'iOS Simulator configured' message"
        : "Not iOS Platform";
    });
  }

  void _testVideo() async {
    setState(() {
      status = "Testing iOS Simulator video...";
      isPlaying = true;
    });

    try {
      // CRITICAL: Auto-reset pattern for iOS Simulator
      await player.reset();
      await Future.delayed(Duration(milliseconds: 100));
      
      await player.setDataSource(testUrl, autoPlay: true);
      
      setState(() {
        status = "Playing test video - Check for video display and console logs";
      });
    } catch (e) {
      setState(() {
        status = "Error: $e";
        isPlaying = false;
      });
    }
  }

  void _stopVideo() async {
    try {
      await player.reset();
      setState(() {
        status = "Stopped - Ready for next test";
        isPlaying = false;
      });
    } catch (e) {
      setState(() {
        status = "Stop error: $e";
      });
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
        title: Text('iOS Simulator Diagnostic'),
        backgroundColor: Colors.red,
      ),
      body: Column(
        children: [
          Container(
            width: double.infinity,
            padding: EdgeInsets.all(16),
            color: Colors.red.shade50,
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  '🔍 iOS Simulator Diagnostic',
                  style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16),
                ),
                SizedBox(height: 8),
                Text('Platform: ${Platform.operatingSystem}'),
                Text('Is iOS: ${Platform.isIOS}'),
                Text('Status: $status'),
                Text('Player State: ${player.value.state}'),
                SizedBox(height: 8),
                Text(
                  '📋 Instructions:',
                  style: TextStyle(fontWeight: FontWeight.bold),
                ),
                Text('1. Run this in iOS Simulator'),
                Text('2. Watch console for "iOS Simulator configured" message'),
                Text('3. Test video playback'),
                Text('4. Report if video shows or just audio'),
              ],
            ),
          ),
          
          Expanded(
            child: Container(
              color: Colors.black,
              child: Center(
                child: AspectRatio(
                  aspectRatio: 16 / 9,
                  child: FijkView(
                    player: player,
                    color: Colors.black,
                    fit: FijkFit.contain,
                  ),
                ),
              ),
            ),
          ),
          
          Padding(
            padding: EdgeInsets.all(16),
            child: Column(
              children: [
                if (!isPlaying)
                  ElevatedButton(
                    onPressed: _testVideo,
                    style: ElevatedButton.styleFrom(
                      backgroundColor: Colors.green,
                      foregroundColor: Colors.white,
                      minimumSize: Size(double.infinity, 48),
                    ),
                    child: Text('🎬 Test iOS Simulator Video'),
                  ),
                if (isPlaying) ...[
                  ElevatedButton(
                    onPressed: _stopVideo,
                    style: ElevatedButton.styleFrom(
                      backgroundColor: Colors.red,
                      foregroundColor: Colors.white,
                      minimumSize: Size(double.infinity, 48),
                    ),
                    child: Text('⏹️ Stop Video'),
                  ),
                ],
                SizedBox(height: 8),
                Text(
                  'This tests the exact same pattern as the working sample app.',
                  style: TextStyle(fontSize: 12, fontStyle: FontStyle.italic),
                  textAlign: TextAlign.center,
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

/// Add this to your main app for testing
class DiagnosticApp extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'iOS Simulator Diagnostic',
      theme: ThemeData(primarySwatch: Colors.red),
      home: IOSSimulatorDiagnostic(),
    );
  }
}
