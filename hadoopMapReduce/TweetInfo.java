import java.io.IOException;
import java.util.*;

import org.apache.hadoop.fs.Path;
import org.apache.hadoop.conf.*;
import org.apache.hadoop.io.*;
import org.apache.hadoop.mapred.*;
import org.apache.hadoop.util.*;

public class TweetInfo {

        public static class TweetMap extends MapReduceBase implements Mapper<LongWritable, Text, Text, IntWritable> {
                private Text word = new Text();

                public void map(LongWritable key, Text value, OutputCollector<Text, IntWritable> output, Reporter reporter) throws IOException {
                        String line = value.toString();
                        StringTokenizer tokenizer = new StringTokenizer(line);
                        while (tokenizer.hasMoreTokens()) {
                                if(line.charAt(0) == 'T'){
					String time = line.substring(13);
					String hour = time.substring(0,2);
					word.set(hour);
					output.collect(word, new IntWritable(1));
					tokenizer.nextToken();
				}
                    		else{
					output.collect(new Text("Not a word"), new IntWritable(1));
					tokenizer.nextToken();
				}            
                        }
                }
        }

        public static class TweetReduce extends MapReduceBase implements Reducer<Text, IntWritable, Text, IntWritable> {
                public void reduce(Text key, Iterator<IntWritable> values, OutputCollector<Text, IntWritable> output, Reporter reporter) throws IOException {
                        int sum = 0;
                        while (values.hasNext()) {
                                sum += values.next().get();
                        }
                        output.collect(key, new IntWritable(sum));
                }
        }

        public static void main(String[] args) throws Exception {
                JobConf conf = new JobConf(TweetInfo.class);
                conf.setJobName("tweethourcount");

                conf.setOutputKeyClass(Text.class);
                conf.setOutputValueClass(IntWritable.class);

                conf.setMapperClass(TweetMap.class);
 		conf.setCombinerClass(TweetReduce.class);
                conf.setReducerClass(TweetReduce.class);

                conf.setInputFormat(TextInputFormat.class);
                conf.setOutputFormat(TextOutputFormat.class);

                FileInputFormat.setInputPaths(conf, new Path(args[0]));
                FileOutputFormat.setOutputPath(conf, new Path(args[1]));

                JobClient.runJob(conf);
        }
}
