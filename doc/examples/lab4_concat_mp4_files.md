why use this `ConcatVideoTS` function is better for concate MP4, instead of using
```
var listname string
	listname += fmt.Sprintf("file %s\n", path.Base(originVideo))
	listname += fmt.Sprintf("file %s\n", path.Base(tailFile))
	concatList := path.Join(tmpFolder, "concat.txt")
	if err := WriteFile(concatList, []byte(listname)); err != nil {
		return err
	}

	// Step 4: Concatenate original video + tail video into a single video file
	// Note: Using -an flag removes audio, so we extract audio separately in Step 5
	fixVideo := path.Join(tmpFolder, "video.mp4")
	args = []string{"-y", "-f", "concat", "-i", concatList, "-an", "-c:v", "copy", fixVideo}
	if _, err := ExecCommand("ConcatVideo", "ffmpeg", args...); err != nil {
		return err
	}

```

```
func ConcatVideoTS(file1, file2 string) (string, error) {
	dir := path.Dir(file1)

	toTS := func(input, output string) error {
		args := []string{
			"-y",
			"-i", input,
			"-c", "copy",
			"-bsf:v", "h264_mp4toannexb",
			"-f", "mpegts",
			output,
		}

		_, err := ExecCommand("ConvertToTS", "ffmpeg", args...)
		return err
	}

	file1TS := path.Join(dir, ".concat_1.ts")
	if err := toTS(file1, file1TS); err != nil {
		return "", err
	}

	file2TS := path.Join(dir, ".concat_2.ts")
	if err := toTS(file2, file2TS); err != nil {
		_ = os.Remove(file1TS)
		return "", err
	}

	output := path.Join(dir, ".concat_output.mp4")

	concatInput := fmt.Sprintf("concat:%s|%s", file1TS, file2TS)

	args := []string{
		"-y",
		"-i", concatInput,
		"-c", "copy",
		"-bsf:a", "aac_adtstoasc",
		output,
	}

	if _, err := ExecCommand("ConcatVideoTS", "ffmpeg", args...); err != nil {
		_ = os.Remove(file1TS)
		_ = os.Remove(file2TS)
		return "", err
	}

	_ = os.Remove(file1TS)
	_ = os.Remove(file2TS)

	return output, nil
}
```