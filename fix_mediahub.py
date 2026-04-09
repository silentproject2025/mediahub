import sys

def remove_lines(lines, start_marker, end_marker):
    new_lines = []
    skip = False
    for line in lines:
        if start_marker in line:
            skip = True
        if not skip:
            new_lines.append(line)
        if skip and end_marker in line:
            skip = False
    return new_lines

with open('MediaHub/MediaHub.ino', 'r') as f:
    lines = f.readlines()

# 1. Fix Camera Color (Surgical)
for i in range(len(lines)):
    if 'camJpeg.setPixelType(RGB565_BIG_ENDIAN);' in lines[i]:
        lines[i] = lines[i].replace('RGB565_BIG_ENDIAN', 'RGB565_LITTLE_ENDIAN')

# 2. Remove RSS Global variables (Lines 539-551 approx)
# We look for // RSS STATE and the next block
new_lines = []
skip = False
for line in lines:
    if '// RSS STATE' in line:
        skip = True
    if not skip:
        new_lines.append(line)
    if skip and 'static int      rssScrollOff  = 0;' in line:
        skip = False
lines = new_lines

# 3. Remove RSS Functions (Individual functions)
def delete_func(lines, func_sig):
    new_lines = []
    skip = False
    brace_count = 0
    found = False
    for line in lines:
        if func_sig in line and not found:
            skip = True
            found = True

        if skip:
            brace_count += line.count('{')
            brace_count -= line.count('}')
            if brace_count == 0 and '{' not in line: # potentially started skipping before {
                 if '}' in line: skip = False
            elif brace_count == 0 and found:
                 skip = False
            continue
        new_lines.append(line)
    return new_lines

lines = delete_func(lines, 'bool rssFetchFeed(int feedIdx)')
lines = delete_func(lines, 'void rssRebuildTicker()')
lines = delete_func(lines, 'void rssFetchAll()')
lines = delete_func(lines, 'void rssTickerUpdate()')
lines = delete_func(lines, 'bool rssNeedsRefresh()')
lines = delete_func(lines, 'void drawRssReader()')

# 4. Remove RSS from menuItems and menuSubtitle
new_lines = []
for line in lines:
    if '"BERITA RSS",' in line: continue
    if '"baca berita RSS",' in line: continue
    if '#define MENU_N 19' in line:
        new_lines.append(line.replace('19', '18'))
        continue
    new_lines.append(line)
lines = new_lines

# 5. Remove RSS ticker from drawMenu
new_lines = []
skip = False
for line in lines:
    if '// RSS Ticker bawah' in line:
        skip = True
    if not skip:
        new_lines.append(line)
    if skip and 'mainBuf.setCursor(4,TICKER_Y+3); mainBuf.print(rssMsg);' in line:
        # We need to skip until the next '}' which closes the else if(wifiConnected)
        pass
    if skip and line.strip() == '}':
        # This is a bit risky, let's look for the start of Menu list
        pass
    if skip and '// Daftar Menu' in line:
        new_lines.append(line)
        skip = False
lines = new_lines

# 6. Remove switch(menuSel) case 7
new_lines = []
skip = False
case_brace_count = 0
for line in lines:
    if 'case 7:' in line and 'RSS' in lines[lines.index(line)+1]:
        skip = True
        case_brace_count = 0

    if skip:
        if 'break;' in line:
            skip = False
        continue
    new_lines.append(line)
lines = new_lines

# 7. Renumber cases 8-18
for i in range(len(lines)):
    for case_num in range(8, 19):
        if f'case {case_num}:' in lines[i]:
            lines[i] = lines[i].replace(f'case {case_num}:', f'case {case_num-1}:')

# 8. Remove other specific lines
new_lines = []
for line in lines:
    if 'ST_RSS_READER,' in line: continue
    if 'rssTickerUpdate();' in line: continue
    if 'rssNeedsRefresh()' in line: continue
    if 'rssFetchAll();' in line: continue
    if 'drawRssReader();' in line: continue
    if 'rssLastFetch = millis();' in line: continue
    if 'clockSyncNTP(); rssLastFetch=millis();' in line:
        new_lines.append(line.replace(' rssLastFetch=millis();', ''))
        continue
    if '// RSS CONFIG' in line:
        skip_config = True
        continue
    # Manual check for RSS CONFIG block
    if 'RSS_FEEDS[]' in line or 'RSS_FEED_COUNT' in line: continue
    if '{ "CNN ID"' in line or '{ "Detik"' in line or '{ "Republika"' in line: continue

    new_lines.append(line)
lines = new_lines

# 9. Global replace TICKER_Y -> SCR_H
for i in range(len(lines)):
    lines[i] = lines[i].replace('TICKER_Y', 'SCR_H')

with open('MediaHub/MediaHub.ino', 'w') as f:
    f.writelines(lines)
