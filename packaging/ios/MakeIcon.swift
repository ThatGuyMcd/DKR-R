import AppKit
import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

let args = CommandLine.arguments
guard args.count == 4, let pixels = Int(args[3]), pixels > 0 else {
    fputs("usage: MakeIcon.swift art.png out.png size\n", stderr)
    exit(1)
}
guard let logo = NSImage(contentsOfFile: args[1]),
      let logoImage = logo.cgImage(forProposedRect: nil, context: nil, hints: nil),
      logoImage.width > 0, logoImage.height > 0 else {
    fputs("could not read \(args[1])\n", stderr)
    exit(1)
}
guard let colorSpace = CGColorSpace(name: CGColorSpace.sRGB),
      let context = CGContext(
        data: nil,
        width: pixels,
        height: pixels,
        bitsPerComponent: 8,
        bytesPerRow: pixels * 4,
        space: colorSpace,
        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
      ) else {
    fputs("could not allocate the icon\n", stderr)
    exit(1)
}

let sky = CGColor(srgbRed: 9.0 / 255.0, green: 169.0 / 255.0, blue: 231.0 / 255.0, alpha: 1)
context.setFillColor(sky)
context.fill(CGRect(x: 0, y: 0, width: pixels, height: pixels))
let size = CGFloat(pixels)
let logoSize = CGSize(width: logoImage.width, height: logoImage.height)
// Square art is already a full icon: draw it edge to edge. iOS applies its own
// corner mask, and the sky fill covers any transparent corners in the art.
let scale = logoSize.width == logoSize.height
    ? size / logoSize.width
    : min((size * 0.86) / logoSize.width, (size * 0.46) / logoSize.height)
let width = logoSize.width * scale
let height = logoSize.height * scale
let dest = CGRect(x: (size - width) / 2, y: (size - height) / 2, width: width, height: height)
context.draw(logoImage, in: dest)
guard let image = context.makeImage() else {
    fputs("could not draw the icon\n", stderr)
    exit(1)
}
let url = URL(fileURLWithPath: args[2]) as CFURL
guard let destination = CGImageDestinationCreateWithURL(url, UTType.png.identifier as CFString, 1, nil) else {
    fputs("could not create \(args[2])\n", stderr)
    exit(1)
}
CGImageDestinationAddImage(destination, image, nil)
guard CGImageDestinationFinalize(destination) else {
    fputs("could not write \(args[2])\n", stderr)
    exit(1)
}
