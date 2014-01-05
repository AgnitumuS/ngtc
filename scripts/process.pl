#!/usr/bin/perl
#
# Batch process media into new format and 
# add thumbnail files, output XML indexing
# in XSPF playlist format.
#
# Chris Kennedy 2009 (C)
#
#import module
use Getopt::Long;

$SIG{INT} = \&catch_signal;
$SIG{HUP} = \&catch_signal;
$SIG{ALRM} = \&catch_signal;

$| = 1;

$ContinueTranscoding = 1;

$MP4BOX = "/usr/local/bin/MP4Box -quiet -inter 1000 -mpeg4";
$INDEX = 0;
$force = 0;

#
$LENGTH = 5; # Thumbnail length
$MYEXT = "mpg"; # Input Files Extensions to search for
$CODEC_DIR="/codecs"; # Location of TRANSCODE.pl Codec profiles
$CREATETHUMB = 1;
$CREATEFULL = 1;
$NOSYNC = 0;
$SINDEX = 0;
#
# get value of input flags
$result = GetOptions (
        "verbose|v" => \$verbose,
	"index" => \$INDEX,
        "test" => \$TEST,
        "nosync" => \$NOSYNC,
        "xmlout|xml" => \$output_xml,
        "autofind|af" => \$AUTOFIND,
        "thumburl|t=s" => \$thumburl,
        "sourceurl|s=s" => \$sourceurl,
        "fullurl|f=s" => \$fullurl,
        "createfull|cf=s" => \$CREATEFULL,
        "createthumb|ct=s" => \$CREATETHUMB,
        "force|frc" => \$force,
        "codecdir|cd=s" => \$CODEC_DIR,
        "thumbcodec|tc=s" => \$thumbcodec,
        "fullcodec|fc=s" => \@fullcodec,
        "extension|e=s" => \$extension,
        "iextension|ie=s" => \$MYEXT,
        "length|l=s" => \$LENGTH,
	"audioonly|ao" => \$audioonly,
	"startindex|si=s" => \$SINDEX,
	"endindex|ei=s" => \$EINDEX,
	"printindex|pi" => \$PINDEX,
        "help|h" => \$help);

if ($help) {
        print "\n$0 Chris Kennedy, (C) VMS 2009\n\n";
        print "Create Flash Video, full and thumbnail versions.\n";
        print "Program will output XML file of videos processed,\n";
        print "won't reprocess ones it has already encoded, but will\n";
        print "print out an XML file of all video seen.\n";
        print "\n";
        print "Usage: -ie mpg -s sources -e mp4 -f fullvideo -t thumbvideo -l 5 -cd /codecs -fc hqv -tc dqvthumb\n\n";
        print "  -v\t\tVerbose, show all output\n";
        print "  -nosync\tTurn off A/V Sync checking\n";
        print "  -xml\t\tOutput XML playlist file\n";
        print "  -force\tEven if old video exists, re-encode\n";
        print "  -index\tUse MP4Box to index output for web streaming/flash\n";
        print "  -ct [0|1]\tCreate thumbnail, default is on (1).\n";
        print "  -cf [0|1]\tCreate Full video, default is on (1).\n";
        print "\n";
        print "  -ao\t\tAudio Only, only encode audio\n";
        print "  -af\t\tAutofind any media file and use it, negates -ie arg\n";
        print "  -ie <fmt>\tInput format [mpg,wmv,mp4], defaults to mpg\n";
        print "  -e <fmt>\tEncoding format Output [mp4|m4a|mp3|flv|wmv]\n";
        print "  -s <dir>\tSource files directory\n";
        print "  -f <dir>\tDestination Full files directory\n";
        print "\n";
        print "  -t <dir>\tDestination Thumbnail files directory\n";
        print "  -l <int>\tLength in seconds of thumbnail files\n";
        print "\n";
        print "  -cd <dir>\tCodec directory, defaults to /codecs\n";
        print "  -tc <file>\tThumbnail CODEC File used\n";
        print "  -fc <file>\tFull CODEC File used\n";
        print "\n";
        print "  -si <int>\tstart index\n";
        print "  -ei <int>\tend index\n";
        print "  -pi\t\tprint index\n";
        print "\n";
        exit(0);
}

$TRANSCODER = "/usr/local/bin/ngtc";

if ($NOSYNC) {
	$TRANSCODER .= " --nosync";
}

if ($force) {
	$TRANSCODER .= " -y";
}

$THUMB_CODEC_MP4="$CODEC_DIR/dqvthumb";
$FULL_CODEC_MP4="$CODEC_DIR/hqv";

$THUMB_CODEC_M4A="";
$FULL_CODEC_M4A="$CODEC_DIR/hqv";

$THUMB_CODEC_FLV="$CODEC_DIR/dqvthumb";
$FULL_CODEC_FLV="$CODEC_DIR/hqv";

$FULL_CODEC_WMV="$CODEC_DIR/dqvthumb";
$THUMB_CODEC_WMV="$CODEC_DIR/dqvwmv";

if ($audioonly || $extension eq "m4a" || $extension eq "mp3") {
	$CREATETHUMB = 0;
	$audioonly = 1;
}
if ($verbose ne "") {
        $VERBOSE = "-d 1";
} else {
        $VERBOSE = "-d 0";
}

if ($thumburl eq '') {
	$THUMBURL = "thumbvideo";
} else {
	$THUMBURL = $thumburl;
}
if ($CREATETHUMB && ! -d $THUMBURL) {
        print "ERROR, no such directory: " . $THUMBURL . "\n";
        exit 1;
}
if ($fullurl eq '') {
	$FULLURL = "fullvideo";
} else {
	$FULLURL = $fullurl;
}
if ($sourceurl eq '') {
	$sourceurl = $FULLURL;
}
if ($CREATEFULL && ! -d $FULLURL) {
        print "ERROR, no such directory: " . $FULLURL . "\n";
        exit 1;
}
if (! -d $sourceurl) {
        print "ERROR, no such directory: " . $sourceurl . "\n";
        exit 1;
}
if ($extension eq '') {
	$EXTENSION = ".mp4";
} else {
	$EXTENSION = "." . $extension;
}

# Check Extension Type
if ($EXTENSION ne ".flv" && $EXTENSION ne ".mp4" && $EXTENSION ne ".mp3" && $EXTENSION ne ".wmv" && $EXTENSION ne ".m4a") {
        print "ERROR, bad extension type, only mp4/flv/wmv/m4a are supported!!!\n";
        exit 1;
}

# Small Thumbnail Video 
if ($thumbcodec ne "") {
        $THUMB_CODEC = $CODEC_DIR . "/" . $thumbcodec;
} else {
        if ($EXTENSION eq ".flv") {
                $THUMB_CODEC = $THUMB_CODEC_FLV;
        } elsif ($EXTENSION eq ".mp4") {
                $THUMB_CODEC = $THUMB_CODEC_MP4;
        } elsif ($EXTENSION eq ".wmv") {
                $THUMB_CODEC = $THUMB_CODEC_WMV;
        }
}
$FFMPEG_THUMB_CMD = "$TRANSCODER $VERBOSE -i ";
$FFMPEG_THUMB_ARGS = " -an -t $LENGTH -c $THUMB_CODEC -o ";

# Check Sanity
if ($CREATETHUMB && ! -f $THUMB_CODEC) {
        print "ERROR with thumbnail codec file " . $THUMB_CODEC . "\n";
        exit 1;
}

# Full Video
if (scalar(@fullcodec) > 0) {
	foreach (@fullcodec) {
		my $cf = $CODEC_DIR . "/" . $_;
		if (-f $cf) {
        		$FULL_CODEC .= "-c " . $cf . " ";
		} else {
			print "ERROR finding codec file $cf\n";
			exit 1;
		}
	}
} else {
        if ($EXTENSION eq ".flv") {
                $FULL_CODEC = "-c " . $FULL_CODEC_FLV;
        } elsif ($EXTENSION eq ".mp4") {
                $FULL_CODEC = "-c " . $FULL_CODEC_MP4;
        } elsif ($EXTENSION eq ".m4a" || $EXTENSION eq ".mp3") {
                $FULL_CODEC = "-c " . $FULL_CODEC_M4A;
        } elsif ($EXTENSION eq ".wmv") {
                $FULL_CODEC = "-c " . $FULL_CODEC_WMV;
        }
}

$FFMPEG_FULL_CMD = "$TRANSCODER $VERBOSE -i ";
if ($audioonly) {
	$FFMPEG_FULL_ARGS = " -vn";
}
if ($EXTENSION eq ".mp3") {
	$FFMPEG_FULL_ARGS .= " -Ef mp3 -Ea libmp3lame -o ";
} else {
	$FFMPEG_FULL_ARGS .= " $FULL_CODEC -o ";
}

if ($AUTOFIND) {
	$MYEXT = "\\*";
}

@fullfiles = `/usr/bin/find $sourceurl  -type f -iname \\*.$MYEXT | sort`;
$file_count = scalar(@fullfiles);

$THUMBURL =~ s/^\.\///g;
$FULLURL =~ s/^\.\///g;
$sourceurl =~ s/^\.\///g;

if ($output_xml) {
	print "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
	print "<playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\">\n";
	print "  <tracklist>\n";
}

$i = 0;
if ($PINDEX) {
	foreach (@fullfiles) {
		if ($SINDEX && $SINDEX > $i) {
			$i++;
			next;
		}
		if ($EINDEX && $EINDEX < $i) {
			$i++;
			next;
		}
		chomp($_);
		if ($AUTOFIND) {
        		$MYEXT = $_;
			$MYEXT =~ s/^.*\///;
        		$MYEXT =~ s/^.*\.//g;
                	if (($sourceurl eq $FULLURL) && $AUTOFIND && ($MYEXT eq "flv" || $MYEXT eq "mp4")) {
                        	next;
                	}
			if ($MYEXT eq "jpg" || $MYEXT eq "txt" || $MYEXT eq "db" || $MYEXT eq "dat") {
				next;
			}
		}
		print "[$i] $_\n";
		$i++;
	}
	if (!$EINDEX) {
		$EINDEX = $file_count;
	}
	print "Showing " . $SINDEX . " to " . $EINDEX . " of " . $i . " files total.\n";
	exit 0;
}

$i = 0;
foreach (@fullfiles) {
	if (!$ContinueTranscoding) {
		print "Exiting...\n";
		exit(1);
	}
	if ($SINDEX && $SINDEX > $i) {
		$i++;
		next;
	}
	if ($EINDEX && $EINDEX < $i) {
		$i++;
		next;
	}
        chomp($_);

	if ($AUTOFIND) {
        	$MYEXT = $_;
		$MYEXT =~ s/^.*\///;
        	$MYEXT =~ s/^.*\.//g;
                if (($sourceurl eq $FULLURL) && $AUTOFIND && ($MYEXT eq "flv" || $MYEXT eq "mp4")) {
                        next;
                }
		if ($MYEXT eq "jpg" || $MYEXT eq "txt" || $MYEXT eq "db" || $MYEXT eq "dat") {
			next;
		}
	}

 	$wmvfile = $_;
 	$fullfile = $_;

	# chop off beginning ./
	$fullfile =~ s/^\.\///g;
	$fullfile =~ s/\.$MYEXT/$EXTENSION/g;

	$thumbnail = $fullfile;
	$annotation = $fullfile;

  	# chop all the path off the file
	$annotation =~ s/^.*\///;
	$annotation =~ s/$EXTENSION//;
	$thumbnail =~ s/^.*\///;

	# make thumbnail path
	$thumbnail = $THUMBURL . "/" . $thumbnail;

	#print "Converting $wmvfile to $fullfile and creating $thumbnail\n";
	if ($sourceurl ne $FULLURL) {
		$fullfile =~ s/$sourceurl/$FULLURL/;
	}
        $thumb_source = $fullfile;
        if ($CREATEFULL == 0) {
                $thumb_source = $wmvfile;
	        $thumb_source =~ s/^\.\///g;
        }

	# Make new directory path for full file
 	$directory = $fullfile;
        $fn = `basename '$fullfile'`;
        chomp($fn);
        $directory =~ s/$fn//g;
        if (! -d "$directory") {
		if (!$TEST) {
               		`mkdir -p $directory`;
	       }
        }

	if ((! -f "$fullfile" || $force) && $CREATEFULL) {
		$create_full_cmd = $FFMPEG_FULL_CMD . $wmvfile . $FFMPEG_FULL_ARGS . $fullfile;
		if ($VERBOSE) {
			print "\n[$i] $create_full_cmd\n";
		}
		if (!$TEST) {
			system($create_full_cmd);
			if (-e $fullfile && $INDEX) {
				# Create index
				$pwd = `pwd`;
				chomp($pwd);
				chdir($directory);
				system("$MP4BOX $fullfile 1>&2 >/dev/null");
				chdir($pwd);
			}
		}
	}
	if ((! -f "$thumbnail" || $force) && $CREATETHUMB) {
		#$create_thumb_cmd = $FFMPEG_THUMB_CMD . $thumb_source . $FFMPEG_THUMB_ARGS . $thumbnail;
		$create_thumb_cmd = $FFMPEG_THUMB_CMD . $wmvfile . $FFMPEG_THUMB_ARGS . $thumbnail;
		if ($VERBOSE) {
			print "\n[$i] $create_thumb_cmd\n";
		}
		if (!$TEST) {
			system($create_thumb_cmd);
		}
	}

	if ($output_xml) {
		print "    <track>\n";
		print "        <location>$thumbnail</location>\n";
		print "        <image>$thumb_source</image>\n";
		print "        <annotation>$annotation</annotation>\n";
		print "    </track>\n";
	}
	$i++;
}

if ($output_xml) {
	print "  </tracklist>\n";
	print "</playlist>\n";
} else {
	if (!$EINDEX) {
		$EINDEX = $file_count;
	}
	print "Finished processing " . $SINDEX . " to " . $EINDEX . " of " . $i . " files total.\n";
}

# catch_signal
sub catch_signal {
        my $signame = shift;
        $ContinueTranscoding = 0;
        $LastSignal = "SIG$signame";
        print CONSOLE_OUTPUT "WARNING: Signal " . $LastSignal . " received\n";
        if ($LastSignal eq "SIGALRM") {
                print CONSOLE_OUTPUT "ERROR: timeout transcoding, exiting.\n";
                exit(1);
        }
}

