import re

with open('MediaHub/MediaHub.ino', 'r') as f:
    content = f.read()

# 1. Camera Color Fix
content = content.replace('camJpeg.setPixelType(RGB565_BIG_ENDIAN);', 'camJpeg.setPixelType(RGB565_LITTLE_ENDIAN);')

# 2. Remove RSS Config Macros
content = re.sub(r'#define RSS_REFRESH_MIN.*?\n', '', content)
content = re.sub(r'#define RSS_HEADLINE_MAX.*?\n', '', content)
content = re.sub(r'#define RSS_TICKER_SPEED.*?\n', '', content)
content = re.sub(r'#define RSS_FETCH_TIMEOUT.*?\n', '', content)

# 3. Remove RssFeed struct and RSS_FEEDS array
content = re.sub(r'struct RssFeed \{.*?\};\s*static const RssFeed RSS_FEEDS\[\].*?\#define RSS_FEED_COUNT\s+\d+', '', content, flags=re.DOTALL)

# 4. Remove ST_RSS_READER from AppState
content = content.replace('ST_RSS_READER,', '')

# 5. Remove RSS State variables and RssHeadline struct
content = re.sub(r'// RSS STATE.*?static int      rssScrollOff  = 0;', '', content, flags=re.DOTALL)

# 6. Remove RSS functions
content = re.sub(r'bool rssFetchFeed\(int feedIdx\)\s*\{.*?\}\n', '', content, flags=re.DOTALL)
content = re.sub(r'void rssRebuildTicker\(\)\s*\{.*?\}\n', '', content, flags=re.DOTALL)
content = re.sub(r'void rssFetchAll\(\)\s*\{.*?\}\n', '', content, flags=re.DOTALL)
content = re.sub(r'void rssTickerUpdate\(\)\s*\{.*?\}\n', '', content, flags=re.DOTALL)
content = re.sub(r'bool rssNeedsRefresh\(\)\s*\{.*?\}\n', '', content, flags=re.DOTALL)
content = re.sub(r'void drawRssReader\(\)\s*\{.*?\}\n', '', content, flags=re.DOTALL)

# 7. Remove RSS Ticker UI block from drawMenu
content = re.sub(r'// RSS Ticker bawah.*?// Daftar Menu', '// Daftar Menu', content, flags=re.DOTALL)

# 8. Update menuItems and menuSubtitle
content = content.replace('"BERITA RSS",', '')
content = content.replace('"baca berita RSS",', '')
content = content.replace('#define MENU_N 19', '#define MENU_N 18')

# 9. Remove case 7 from switch(menuSel) and renumber
# Surgical removal of case 7
content = re.sub(r'          case 7:.*?break;', '', content, flags=re.DOTALL)
for i in range(8, 19):
    content = content.replace(f'case {i}:', f'case {i-1}:')

# 10. Remove ST_RSS_READER case block
content = re.sub(r'    case ST_RSS_READER:.*?break;', '', content, flags=re.DOTALL)

# 11. Final cleanup of any remaining calls
content = re.sub(r'if\(wifiConnected&&rssNeedsRefresh\(\)&&!rssFetching\) rssFetchAll\(\);', '', content)
content = re.sub(r'if\(appState==ST_MENU\) rssTickerUpdate\(\);', '', content)
content = content.replace('rssLastFetch = millis();', '')
content = content.replace('rssLastFetch=millis();', '')

# 12. Adjust layout
content = content.replace('TICKER_Y', 'SCR_H')

with open('MediaHub/MediaHub.ino', 'w') as f:
    f.write(content)
