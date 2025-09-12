import 'package:flutter/material.dart';
import 'package:fijkplayer/fijkplayer.dart';

/// Simple video test demonstrating FijkPlayer functionality
/// Includes auto-reset for reliable state management
class SimpleVideoTest extends StatefulWidget {
  @override
  _SimpleVideoTestState createState() => _SimpleVideoTestState();
}

class _SimpleVideoTestState extends State<SimpleVideoTest> {
  final FijkPlayer player = FijkPlayer();
  String status = "Ready";
  int currentVideoIndex = 0;
  
  final List<Map<String, String>> testVideos = [
    {
      "name": "Sintel Trailer",
      "url": "https://media.w3.org/2010/05/sintel/trailer.mp4"
    },
    {
      "name": "Big Buck Bunny 1080p", 
      "url": "https://test-videos.co.uk/vids/bigbuckbunny/mp4/h264/1080/Big_Buck_Bunny_1080_10s_1MB.mp4"
    },
    {
      "name": "Big Buck Bunny 720p",
      "url": "https://test-videos.co.uk/vids/bigbuckbunny/mp4/h264/720/Big_Buck_Bunny_720_10s_1MB.mp4"
    }
  ];

  @override
  void initState() {
    super.initState();
    setState(() {
      status = "Ready to play videos";
    });
  }

  void _playCurrentVideo() async {
    setState(() {
      status = "Loading ${testVideos[currentVideoIndex]['name']}...";
    });
    
    try {
      // Reset player before loading new video for reliable state management
      await player.reset();
      await Future.delayed(Duration(milliseconds: 100));
      
      await player.setDataSource(testVideos[currentVideoIndex]['url']!, autoPlay: true);
      setState(() {
        status = "Playing ${testVideos[currentVideoIndex]['name']}";
      });
    } catch (e) {
      setState(() {
        status = "Error: $e";
      });
    }
  }

  void _nextVideo() {
    setState(() {
      currentVideoIndex = (currentVideoIndex + 1) % testVideos.length;
    });
    _playCurrentVideo();
  }

  void _stopAndReset() async {
    try {
      await player.reset();
      setState(() {
        status = "Reset complete - Ready to play";
      });
    } catch (e) {
      setState(() {
        status = "Reset error: $e";
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
        title: Text('FijkPlayer Video Test'),
        backgroundColor: Colors.blue,
      ),
      body: Column(
        children: [
          Container(
            width: double.infinity,
            padding: EdgeInsets.all(16),
            color: Colors.blue.shade50,
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  'FijkPlayer Test',
                  style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16),
                ),
                SizedBox(height: 8),
                Text('Current: ${testVideos[currentVideoIndex]['name']}',
                     style: TextStyle(fontWeight: FontWeight.bold)),
                Text('Status: $status'),
                Text('State: ${player.value.state}'),
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
                ElevatedButton(
                  onPressed: _playCurrentVideo,
                  style: ElevatedButton.styleFrom(
                    backgroundColor: Colors.blue,
                    foregroundColor: Colors.white,
                    minimumSize: Size(double.infinity, 48),
                  ),
                  child: Text('▶️ Play ${testVideos[currentVideoIndex]['name']}'),
                ),
                SizedBox(height: 8),
                ElevatedButton(
                  onPressed: _nextVideo,
                  style: ElevatedButton.styleFrom(
                    backgroundColor: Colors.green,
                    foregroundColor: Colors.white,
                    minimumSize: Size(double.infinity, 48),
                  ),
                  child: Text('⏭️ Next Video'),
                ),
                SizedBox(height: 8),
                ElevatedButton(
                  onPressed: _stopAndReset,
                  style: ElevatedButton.styleFrom(
                    backgroundColor: Colors.orange,
                    foregroundColor: Colors.white,
                    minimumSize: Size(double.infinity, 48),
                  ),
                  child: Text('🔄 Reset Player'),
                ),
                SizedBox(height: 16),
                Text(
                  'Test different video formats and resolutions.',
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

class SimpleTestApp extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'FijkPlayer Test',
      theme: ThemeData(primarySwatch: Colors.blue),
      home: SimpleVideoTest(),
    );
  }
}

void main() {
  runApp(SimpleTestApp());
}