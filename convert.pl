#!/usr/bin/perl

use warnings;
use strict;
use File::Basename;
use autodie;
use File::Path;
use File::Find;
use Parallel::Simple qw( prun );

my $path = $ARGV[0];
die "Please specify which directory to search"
  unless -d $path;

sub min {
    return ( $_[0], $_[1] )[ $_[0] > $_[1] ];
}

sub par_list {
    my $one = $_[0];
    my $len = $_[1];

    my $chunk_size = $len / 8;
    my @subs;
    for ( my $i = 0 ; $i < $len ; $i += $chunk_size + 1 ) {
        my $j = $i;
        push @subs, sub { $one->( $j, min( $j + $chunk_size, $len - 1 ) ) };
    }
    prun(@subs) or die( Parallel::Simple::errplus() );
}

sub to_txt {
    my @files = glob( $path . '/*.zip' );

    my $one = sub {
        for ( @files[ $_[0] .. $_[1] ] ) {
            print("$_\n");
            my ( $name, $dir, $ext ) = fileparse( $_, '.zip' );
            my $newdir = $dir . $name;
            system("mkdir -p $newdir");
            system("unzip -qq $_ -d $newdir");

            for ( glob( $newdir . '/*.fb2' ) ) {
                my ( $name, $dir, $ext ) = fileparse( $_, '.fb2' );
                system("xml_grep 'p' $_ --text_only > $dir/$name.txt");
            }
            unlink glob( $newdir . '/*.fb2' );
            unlink $_;
        }
    };

    par_list( $one, scalar @files );
}

sub archive {
    opendir my $dh, $path or die "$0: opendir: $!";
    my @dirs = grep { -d "$path/$_" && !/^\.{1,2}$/ } readdir($dh);
    closedir $dh;

    my $one = sub {
        for ( @dirs[ $_[0] .. $_[1] ] ) {
            system("zip -rj -qq $ARGV[1]/$_.zip $path/$_");
        }
    };

    par_list( $one, scalar @dirs );
}

sub unarchive {
    my @files = glob( $path . '/*.zip' );

    my $one = sub {
        for ( @files[ $_[0] .. $_[1] ] ) {
            my ( $name, $dir, $ext ) = fileparse( $_, '.zip' );
            my $newdir = $dir . $name;
            system("mkdir -p $newdir");
            system("unzip -qq $_ -d $newdir");
            unlink $_;
        }
    };

    par_list( $one, scalar @files );
}

sub unlink_all_non_russian {
    system("rg \"и\" --files-without-match $path > $path/nonru");
    open( my $FH, '<', "$path/nonru" ) or die $!;
    while (<$FH>) {
        $_ =~ s/^\s+|\s+$//g;
        unlink $_;
    }
    close(FH);
}

# to_txt()
# unlink_from_file()
# archive()

# unarchive()
