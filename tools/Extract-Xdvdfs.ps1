param(
    [Parameter(Mandatory=$true)][string]$IsoPath,
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [switch]$ListOnly
)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
public static class XdvdfsExtractor {
    static FileStream iso;
    static long partition;
    static string root;
    static bool listOnly;
    static HashSet<long> directories;
    static byte[] Read(long offset, int count) {
        if (offset < 0 || offset > iso.Length - count) throw new InvalidDataException("ISO range outside file");
        byte[] data = new byte[count]; iso.Position = offset;
        int done = 0; while (done < count) { int n=iso.Read(data,done,count-done); if(n==0) throw new EndOfStreamException(); done+=n; }
        return data;
    }
    static void DirectoryTable(uint sector, uint size, string relative, int depth) {
        if(depth > 64 || size > 67108864) throw new InvalidDataException("Invalid directory depth/size");
        if(size==0) return;
        long location=partition+(long)sector*2048;
        if(!directories.Add(location)) throw new InvalidDataException("Repeated directory");
        byte[] table=Read(location,checked((int)size));
        var pending=new Stack<int>(); var visited=new HashSet<int>(); pending.Push(0);
        while(pending.Count>0) {
            int pos=pending.Pop();
            if(!visited.Add(pos) || pos<0 || pos>table.Length-14) throw new InvalidDataException("Invalid directory entry");
            ushort left=BitConverter.ToUInt16(table,pos), right=BitConverter.ToUInt16(table,pos+2);
            uint fileSector=BitConverter.ToUInt32(table,pos+4), length=BitConverter.ToUInt32(table,pos+8);
            byte attributes=table[pos+12], nameLength=table[pos+13];
            if(pos+14+nameLength>table.Length) throw new InvalidDataException("Invalid name length");
            string name=Encoding.ASCII.GetString(table,pos+14,nameLength);
            if(name.Length==0 || name=="." || name==".." || name.IndexOfAny(Path.GetInvalidFileNameChars())>=0) throw new InvalidDataException("Unsafe filename");
            string rel=Path.Combine(relative,name), destination=Path.GetFullPath(Path.Combine(root,rel));
            if(!destination.StartsWith(root+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Path escapes output directory");
            if(left!=0) pending.Push(left*4); if(right!=0) pending.Push(right*4);
            if((attributes&16)!=0) {
                if(!listOnly) System.IO.Directory.CreateDirectory(destination);
                DirectoryTable(fileSector,length,rel,depth+1);
            } else {
                Console.WriteLine(length.ToString()+"\t"+rel);
                if(listOnly) continue;
                long start=partition+(long)fileSector*2048;
                if(start<0 || start>iso.Length-length) throw new InvalidDataException("File outside ISO");
                System.IO.Directory.CreateDirectory(Path.GetDirectoryName(destination));
                using(var output=new FileStream(destination,FileMode.CreateNew,FileAccess.Write)) {
                    iso.Position=start; byte[] buffer=new byte[1048576]; long remaining=length;
                    while(remaining>0) {int n=iso.Read(buffer,0,(int)Math.Min(buffer.Length,remaining)); if(n==0) throw new EndOfStreamException(); output.Write(buffer,0,n); remaining-=n;}
                }
            }
        }
    }
    public static void Run(string source,string output,bool listing) {
        root=Path.GetFullPath(output).TrimEnd(Path.DirectorySeparatorChar); listOnly=listing; directories=new HashSet<long>();
        using(iso=File.OpenRead(source)) {
            foreach(long candidate in new long[]{0,0xFD90000,0x2080000}) {
                byte[] header=Read(candidate+0x10000,2048);
                if(Encoding.ASCII.GetString(header,0,20)!="MICROSOFT*XBOX*MEDIA") continue;
                partition=candidate;
                DirectoryTable(BitConverter.ToUInt32(header,20),BitConverter.ToUInt32(header,24),"",0);
                return;
            }
            throw new InvalidDataException("XDVDFS volume not found");
        }
    }
}
'@
[XdvdfsExtractor]::Run($IsoPath, $OutputPath, $ListOnly.IsPresent)
