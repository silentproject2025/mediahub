import re

with open('MediaHub/MediaHub.ino', 'r') as f:
    content = f.read()

# Find the switch(menuSel) block
match = re.search(r'switch\(menuSel\) \{(.*?)\n\s*\}', content, re.DOTALL)
if match:
    switch_body = match.group(1)

    # Shift existing cases
    new_body = ""
    # Add new case 0
    new_body += '\n          case 0:\n            if(!requireWifi("AI CHAT")) break;\n            appState=ST_AI_MODE; aiModeSel=0; drawAIModeSelection(); pushFrame(); btnFlushAll(); break;'

    # Regex to find each case
    cases = re.findall(r'(case (\d+):.*?(?=case \d+:|break;)\s*break;)', switch_body, re.DOTALL)
    for c_text, c_num in cases:
        shifted_num = int(c_num) + 1
        shifted_case = c_text.replace(f'case {c_num}:', f'case {shifted_num}:')
        new_body += "\n" + shifted_case

    content = content.replace(switch_body, new_body)

with open('MediaHub/MediaHub.ino', 'w') as f:
    f.write(content)
