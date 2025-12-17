#!/bin/bash
# Remove invalid shared_preferences library reference from Xcode project
sed -i '' 's/-l"shared_preferences" //g' Runner.xcodeproj/project.pbxproj
sed -i '' 's/ -l"shared_preferences"//g' Runner.xcodeproj/project.pbxproj
echo "Fixed linker flags"
