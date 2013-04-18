#!/usr/bin/perl

my @table,@a;

open(X,"<","CP437.TXT") || die;
while (my $line = <X>) {
	chomp $line;
	$line =~ s/^[\t ]+//;
	$line =~ s/\#.*$//;
	next if $line =~ m/^\x1A/;
	@a = split(/[\t ]+/,$line);
	next if @a < 2;
	next if $a[0] !~ m/^0x/;
	$a[0] = oct($a[0]);
	next if $a[0] > 255;
	$a[1] = oct($a[1]);
	$table[$a[0]] = $a[1];
}
close(X);

print "uint16_t __table__[256] = {\n";
for ($i=0;$i < 256;$i++) {
	print sprintf("0x%04x",$table[$i]);
	if ($i == 255) {
	}
	elsif (($i&15) == 15) {
		print ",\n";
	}
	else {
		print ",";
	}
}
print "};\n";
