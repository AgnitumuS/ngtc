#!/usr/bin/perl
#
use Getopt::Long;

$DEBUG_LEVEL = 1;
$BEGIN_SECS = 0;
$END_SECS = 0;

$EXT = "mpg";
$result = GetOptions (
        "verbose|v" => \$verbose,
        "ext|e" => \$EXT,
        "begin|b=s" => \$BEGIN_SECS,
        "total|t=s" => \$END_SECS,
        "debuglevel|dl=s" => \$DEBUG_LEVEL,
        "output_file|o=s" => \$OUTPUT_FILE,
        "dir|d=s" => \$DIRECTORY);

if (!$DIRECTORY || ! -d $DIRECTORY) {
	print "Error: No such directory $DIRECTORY\n";
	exit 1;
}

if (!$OUTPUT_FILE) {
	print "Error: No output file given\n";
	exit 1;
}

@files = `/usr/bin/find $DIRECTORY  -type f -iname \\*.$EXT | sort`;
$file_count = scalar(@files);

if ($file_count <= 1) {
	print "Error: No files found to combine\n";
	exit 1;
}
print "Combining $file_count files\n";

foreach (@files) {
	chomp($_);
	$cmb .= "-i " . $_ . " ";
}

$cmd_line = "/usr/local/bin/ngtc " . $cmb;
#$cmd_line .= "-Rt ";
$cmd_line .= "-R ";
$cmd_line .= "-d $DEBUG_LEVEL -y -o " . $OUTPUT_FILE;

if ($BEGIN_SECS) {
	$cmd_line .= " -b $BEGIN_SECS";
}
if ($END_SECS) {
	$cmd_line .= " -t $END_SECS";
}

#$cmd_line .= " 2>/tmp/out";

print "$cmd_line\n";
system($cmd_line);
