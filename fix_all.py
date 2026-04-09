import re

with open('MediaHub/MediaHub.ino', 'r') as f:
    lines = f.readlines()

def find_func_end(lines, start_idx):
    brace_count = 0
    found_open = False
    for i in range(start_idx, len(lines)):
        brace_count += lines[i].count('{')
        brace_count -= lines[i].count('}')
        if lines[i].count('{') > 0:
            found_open = True
        if found_open and brace_count == 0:
            return i
    return -1

# 1. Camera Color Fix
for i in range(len(lines)):
    if 'camJpeg.setPixelType(RGB565_BIG_ENDIAN);' in lines[i]:
        lines[i] = lines[i].replace('RGB565_BIG_ENDIAN', 'RGB565_LITTLE_ENDIAN')

# 2. Collect indices of RSS components to remove
to_remove = set()

# Functions
func_sigs = [
    'bool rssFetchFeed(int feedIdx)',
    'void rssRebuildTicker()',
    'void rssFetchAll()',
    'void rssTickerUpdate()',
    'bool rssNeedsRefresh()',
    'void drawRssReader()'
]

for sig in func_sigs:
    for i in range(len(lines)):
        if sig in lines[i]:
            end = find_func_end(lines, i)
            if end != -1:
                for j in range(i, end + 1):
                    to_remove.add(j)
            break

# Blocks
blocks = [
    ('// RSS CONFIG', 'RSS_FEED_COUNT'),
    ('// RSS STATE', 'rssScrollOff'),
    ('// RSS Ticker bawah', 'mainBuf.clearClipRect();')
]

for start_m, end_m in blocks:
    start_idx = -1
    for i in range(len(lines)):
        if start_m in lines[i]:
            start_idx = i
        if start_idx != -1 and end_m in lines[i]:
            for j in range(start_idx, i + 1):
                to_remove.add(j)
            start_idx = -1
            break

# Menu Items and Subtitles
for i in range(len(lines)):
    if '"BERITA RSS",' in lines[i]:
        to_remove.add(i)
    if '"baca berita RSS",' in lines[i]:
        to_remove.add(i)
    if 'ST_RSS_READER,' in lines[i]:
        to_remove.add(i)
    if 'case ST_RSS_READER:' in lines[i]:
        end = find_func_end(lines, i)
        if end != -1:
            for j in range(i, end + 1):
                to_remove.add(j)

# Menu switch case 7
for i in range(len(lines)):
    if 'case 7:' in lines[i] and 'appState=ST_RSS_READER;' in lines[i+1]:
        end = find_func_end(lines, i) # This might not work if it's just case 7: ... break;
        # Let's just look for the break;
        for j in range(i, len(lines)):
            to_remove.add(j)
            if 'break;' in lines[j]:
                break
        break

# Remaining calls and renumbering
new_lines = []
for i in range(len(lines)):
    if i in to_remove:
        continue

    line = lines[i]
    if 'rssTickerUpdate();' in line: continue
    if 'rssNeedsRefresh()' in line: continue
    if 'rssFetchAll();' in line: continue
    if 'drawRssReader();' in line: continue
    if 'rssLastFetch = millis();' in line: continue
    if 'rssLastFetch=millis();' in line:
        line = line.replace(' rssLastFetch=millis();', '')

    if '#define MENU_N 19' in line:
        line = line.replace('19', '18')

    # Renumber cases 8-18
    for case_num in range(8, 19):
        # Use word boundaries to avoid matching things like case 180
        line = re.sub(r'\bcase ' + str(case_num) + r'\b:', 'case ' + str(case_num-1) + ':', line)

    # Adjust layout
    if 'const int MENU_BOT   = TICKER_Y - 2;' in line:
        line = '  const int MENU_BOT   = SCR_H - 18;\n'

    line = line.replace('TICKER_Y', 'SCR_H')

    new_lines.append(line)

with open('MediaHub/MediaHub.ino', 'w') as f:
    f.writelines(new_lines)
