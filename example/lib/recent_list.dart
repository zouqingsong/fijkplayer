import 'package:flutter/material.dart';
// import 'package:shared_preferences/shared_preferences.dart';  // Temporarily disabled
import 'dart:convert';

import 'app_bar.dart';
import 'media_item.dart';

const List<MediaUrl> samples = [
  MediaUrl(
      title: "Aliyun", url: "http://player.alicdn.com/video/aliyunmedia.mp4"),
  MediaUrl(
      title: "http 404", url: "https://fijkplayer.befovy.com/butterfly.flv"),
  MediaUrl(title: "assets file", url: "asset:///assets/butterfly.mp4"),
  MediaUrl(title: "assets file", url: "asset:///assets/birthday.mp4"),
  MediaUrl(title: "assets file 404", url: "asset:///assets/beebee.mp4"),
  MediaUrl(
      title: "Protocol not found", url: "noprotocol://assets/butterfly.mp4"),
  MediaUrl(
      title: "rtsp test",
      url: "rtsp://wowzaec2demo.streamlock.net/vod/mp4:BigBuckBunny_115k.mov"),
  MediaUrl(
      title: "Sample Video 360 * 240",
      url:
          "https://sample-videos.com/video123/flv/240/big_buck_bunny_240p_10mb.flv"),
  MediaUrl(
      title: "bipbop basic master playlist",
      url:
          "http://devimages.apple.com.edgekey.net/streaming/examples/bipbop_4x3/bipbop_4x3_variant.m3u8"),
  MediaUrl(
      title: "bipbop basic 400x300 @ 232 kbps",
      url:
          "http://devimages.apple.com.edgekey.net/streaming/examples/bipbop_4x3/gear1/prog_index.m3u8"),
  MediaUrl(
      title: "bipbop basic 640x480 @ 650 kbps",
      url:
          "http://devimages.apple.com.edgekey.net/streaming/examples/bipbop_4x3/gear2/prog_index.m3u8"),
  MediaUrl(
      title: "bipbop basic 640x480 @ 1 Mbps",
      url:
          "http://devimages.apple.com.edgekey.net/streaming/examples/bipbop_4x3/gear3/prog_index.m3u8"),
  MediaUrl(
      title: "bipbop basic 960x720 @ 2 Mbps",
      url:
          "http://devimages.apple.com.edgekey.net/streaming/examples/bipbop_4x3/gear4/prog_index.m3u8"),
  MediaUrl(
      title: "bipbop basic 22.050Hz stereo @ 40 kbps",
      url:
          "http://devimages.apple.com.edgekey.net/streaming/examples/bipbop_4x3/gear0/prog_index.m3u8"),
  MediaUrl(
      title: "bipbop advanced master playlist",
      url:
          "http://devimages.apple.com.edgekey.net/streaming/examples/bipbop_16x9/bipbop_16x9_variant.m3u8"),
  MediaUrl(
      title: "bipbop advanced 416x234 @ 265 kbps",
      url:
          "http://devimages.apple.com.edgekey.net/streaming/examples/bipbop_16x9/gear1/prog_index.m3u8"),
  MediaUrl(
      title: "bipbop advanced 640x360 @ 580 kbps",
      url:
          "http://devimages.apple.com.edgekey.net/streaming/examples/bipbop_16x9/gear2/prog_index.m3u8"),
  MediaUrl(
      title: "bipbop advanced 960x540 @ 910 kbps",
      url:
          "http://devimages.apple.com.edgekey.net/streaming/examples/bipbop_16x9/gear3/prog_index.m3u8"),
  MediaUrl(
      title: "bipbop advanced 1280x720 @ 1 Mbps",
      url:
          "http://devimages.apple.com.edgekey.net/streaming/examples/bipbop_16x9/gear4/prog_index.m3u8"),
  MediaUrl(
      title: "bipbop advanced 1920x1080 @ 2 Mbps",
      url:
          "http://devimages.apple.com.edgekey.net/streaming/examples/bipbop_16x9/gear5/prog_index.m3u8"),
  MediaUrl(
      title: "bipbop advanced 22.050Hz stereo @ 40 kbps",
      url:
          "http://devimages.apple.com.edgekey.net/streaming/examples/bipbop_16x9/gear0/prog_index.m3u8"),
];

class SamplesScreen extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: FijkAppBar.defaultSetting(title: "Online Samples"),
      body: ListView.builder(
          itemCount: samples.length,
          itemBuilder: (BuildContext context, int index) {
            MediaUrl mediaUrl = samples[index];
            return MediaItem(mediaUrl: mediaUrl);
          }),
    );
  }
}

class RecentMediaList extends StatefulWidget {
  @override
  _RecentMediaListState createState() => _RecentMediaListState();
}

class _RecentMediaListState extends State<RecentMediaList> {
  int recentCount = 0;
  int newestId = 0;
  // SharedPreferences? prefs;  // Temporarily disabled
  ScrollController _controller = ScrollController();

  @override
  void initState() {
    super.initState();
    asyncSetup();
  }

  asyncSetup() async {
    // prefs = await SharedPreferences.getInstance();
    loadHistory();
  }

  loadHistory() {
    // Temporarily disabled due to SharedPreferences iOS linking bug
    setState(() {
      recentCount = 0;
      newestId = 0;
    });
  }

  @override
  Widget build(BuildContext context) {
    // Show empty state when SharedPreferences is disabled
    if (recentCount == 0) {
      return Center(
        child: Text(
          'Recent list temporarily disabled\nUse video_page to test native player',
          textAlign: TextAlign.center,
        ),
      );
    }
    
    // Dead code - never reached since recentCount is always 0
    return Center(child: Text('No recent videos'));
  }
}

Future<void> addToHistory(MediaUrl mediaUrl) async {
  // Temporarily disabled due to SharedPreferences iOS linking bug
  return;
}

