// wavファイルを読み込んで信号強度から有効性を評価するプログラム
package main

import (
	"fmt"
	"regexp"
	"math"
	"os"
	"strings"
	"io"
	"log"
	"path/filepath"
	"flag"

	"github.com/youpy/go-wav"
)

func main() {
	var rms_th float64 // RMS値がこれ未満なら有効データとみなさない。単位%
	var clipped_ratio_th float64 // クリッピングデータ点の割合がこの値より大きい場合、有効データとみなさない。単位%
	var disable_unstability_check  bool

	logger := log.New(os.Stderr, "[info] ", log.LstdFlags)

	myname := filepath.Base(os.Args[0])
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, `Usage of %s:
   %s [OPTIONS] soundfile
Options
`, myname, myname)
		flag.PrintDefaults()
		fmt.Fprintf(os.Stderr, `  soundfile: sound data file whose format is wav.
`)
	}

	flag.Float64Var(&rms_th, "tr", 1.0, "Lower limit of the RMS (%). If RMS is lower than this value, the data is non-effective.")
	flag.Float64Var(&clipped_ratio_th, "tc", 0.0, "Upper limit of the clipped samples ratio (%). If the ratio of clipped samples is higher than this value, the data is non-effective.")
	flag.BoolVar(&disable_unstability_check, "d", false, "Disable the checking process regarding unstability and abnormality.")
	flag.Parse()
	// 引数のチェック
	if len(flag.Args()) != 1 {
		flag.Usage()
		return 
	}


	// wave形式ファイルの読み込み
	wav_file := flag.Arg(0)
	wav_file_dirname := filepath.Dir(wav_file)
	wav_file_basename := get_filebasename(wav_file)
	
	// 各ファイルの信号強度を評価

	// wave形式ファイルかどうかのチェック
	if !strings.HasSuffix(wav_file, ".wav") {
		fmt.Println("you should specify wav file.")
		return
	}
	// wave形式ファイルの読み込み
	iwav, err := os.Open(wav_file)
	if err != nil {
		fmt.Println(err)
		os.Rename(wav_file, wav_file_dirname + "/" + wav_file_basename + ".unavailable.wav")
		logger.Println("change filename from ", wav_file, " to ", wav_file_dirname + "/" + wav_file_basename + ".unavailable.wav")
		return
	}
	reader := wav.NewReader(iwav)
	defer iwav.Close()

	wavformat, err_rd := reader.Format()
	if err_rd != nil {
			panic(err_rd)
	}
	//fmt.Println("wavformat.SampleRate:", wavformat.SampleRate)

	wav_samples := make([]float64, 0)
	clipped_samples := make([]float64, 0)
	var rms, ave float64
	var max, min float64
	for {
		samples, err := reader.ReadSamples()
		if err == io.EOF {
			break
		}

		for _, sample := range samples {
			v := reader.FloatValue(sample, 0) // get the value of the sample (0 = left channel)
			wav_samples = append(wav_samples, v)
			rms += v * v
			ave += v
			max = max_float(max, v)
			min = min_float(min, v)
			
			if (v >= 0.98) || (v <= -0.98) {
				clipped_samples = append(clipped_samples, v)
			}
		}
	}

	n_samples := len(wav_samples)
	rms = math.Sqrt(rms / float64(n_samples))
	ave = ave / float64(n_samples)
	/*
		fmt.Print("Samples: " + fmt.Sprintf("%d", n_samples) + "\n")
		fmt.Println("rms:", rms)
		fmt.Println("ave:", ave)
		fmt.Println("max:", max)
		fmt.Println("min:", min)
	*/

	// check effectiveness of the wav file

	// check1 rms is weak or not
	if (rms < rms_th / 100.0) {
		fmt.Printf("rms (%v) is too weak. tune the gain.\n", rms)
		os.Rename(wav_file, wav_file_dirname + "/" + wav_file_basename + ".weak.wav")
		logger.Println("change filename from ", wav_file, " to ", wav_file_dirname + "/" + wav_file_basename + ".weak.wav")
		return
	}

	// check2 max and min are not overflow (clipped)
	clipped_ratio := float64(len(clipped_samples)) / float64(n_samples)
	if clipped_ratio > clipped_ratio_th / 100.0 {
		fmt.Printf("clipping is detected. tune the gain. clipping ratio (%v) > threshold (%v)\n", clipped_ratio, clipped_ratio_th / 100.0)
		os.Rename(wav_file, wav_file_dirname + "/" + wav_file_basename + ".clipped.wav")
		logger.Println("change filename from ", wav_file, " to ", wav_file_dirname + "/" + wav_file_basename + ".clipped.wav")
		return
	}

	var segment_size int
	n_segments := n_samples / (int(wavformat.SampleRate) * 2)
	if n_segments > 0 {
		segment_size = int(n_samples / n_segments)
	} else {
		segment_size = n_samples
	}
	prev_rms := rms // set initial value for rms of segment as the rms of the whole wav file
	for i := 0; i < n_samples; i += segment_size {
		rms_segment := 0.0
		max_segment := -1.0
		min_segment := 1.0
		ave_segment := 0.0
		for j := i; j < i+segment_size; j++ {
			rms_segment += wav_samples[j] * wav_samples[j]
			max_segment = max_float(max_segment, wav_samples[j])
			min_segment = min_float(min_segment, wav_samples[j])
			ave_segment += wav_samples[j]
		}
		fmt.Println("segment:", i, " rms:", rms, " ave:", ave, " max:", max, " min:", min)

		if (disable_unstability_check == false) {
			// check3: stability: rms level isn't changed for all range
			rms_segment = math.Sqrt(rms_segment / float64(segment_size))
			if (rms_segment / prev_rms) < 0.50 || (rms_segment / prev_rms) > 1.50 {
				fmt.Printf("unstable: rms_segment / prev_rms = %f\n", rms_segment/prev_rms)
				os.Rename(wav_file, wav_file_dirname + "/" + wav_file_basename + ".unstable.wav")
				logger.Println("change filename from ", wav_file, " to ", wav_file_dirname + "/" + wav_file_basename + ".unstable.wav")
				return
			}

			// check4: shape of freq. data: both max and abs(min) are larger than rms
			ave_segment = ave_segment / float64(segment_size)
			if (max_segment < ave_segment+rms_segment*1.5) && (min_segment > ave_segment-rms_segment*1.5) {
				fmt.Print("abnormal shape: ")
				fmt.Printf("max_segment(%f) < ave_segment(%f)+rms_segment(%f)*1.5 for segment %d\n", min_segment, ave_segment, rms_segment, i)
				fmt.Printf("min_segment(%f) > ave_segment(%f)-rms_segment(%f)*1.5 for segment %d\n", min_segment, ave_segment, rms_segment, i)
				os.Rename(wav_file, wav_file_dirname + "/" + wav_file_basename + ".abnormal.wav")
				logger.Println("change filename from ", wav_file, " to ", wav_file_dirname + "/" + wav_file_basename + ".abnormal.wav")
				return
			}
		}
		prev_rms = rms_segment
	}

	fbase := filepath.Base(wav_file[:len(wav_file)-len(filepath.Ext(wav_file))])
	if fbase != wav_file_basename {
		os.Rename(wav_file, wav_file_dirname + "/" + wav_file_basename + ".wav")
		logger.Println("change filename from ", wav_file, " to ", wav_file_dirname + "/" + wav_file_basename + ".wav")
	}
	fmt.Println("effective")
}

func max_float(a, b float64) float64 {
	if a > b {
		return a
	}
	return b
}

func min_float(a, b float64) float64 {
	if a < b {
		return a
	}
	return b
}

func get_filebasename(filename string) string {
	re := regexp.MustCompile("(?i)^(.+)(\\.weak|\\.unstable|\\.clipped|\\.abnormal)(\\.wav|\\.csv)$")
	if re.MatchString(filename) {
		return re.ReplaceAllString(filepath.Base(filename), "$1")
	} else {
		return filepath.Base(filename[:len(filename)-len(filepath.Ext(filename))])
	}
}
