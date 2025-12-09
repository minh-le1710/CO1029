import pathlib

# Đường dẫn file log thô và file csv đầu ra
raw_path = pathlib.Path(r"D:\CO1029\raw_log.txt")
csv_path = pathlib.Path(r"D:\CO1029\dataset_th.csv")

print("Input log:", raw_path)
print("Output csv:", csv_path)

# Luật gán nhãn:
# 0 = NORMAL  : T < 30 và H < 70
# 1 = WARNING : 30 <= T < 35 hoặc 70 <= H < 85
# 2 = CRITICAL: T >= 35 hoặc H >= 85
def label_from_th(temp, hum):
    if temp < 30.0 and hum < 70.0:
        return 0
    elif (temp < 35.0 and hum < 85.0):
        return 1
    else:
        return 2

count = 0
with raw_path.open("r", encoding="utf-8", errors="ignore") as fin, \
     csv_path.open("w", encoding="utf-8") as fout:

    # header
    fout.write("temp,hum,label\n")

    for line in fin:
        line = line.strip()
        if line.startswith("DATA,"):
            try:
                _, rest = line.split("DATA,", 1)
                parts = rest.split(",")
                if len(parts) < 2:
                    continue
                temp = float(parts[0])
                hum  = float(parts[1])
            except Exception:
                continue

            lbl = label_from_th(temp, hum)
            fout.write(f"{temp:.2f},{hum:.2f},{lbl}\n")
            count += 1

print(f"Done. {count} samples written to {csv_path}")
