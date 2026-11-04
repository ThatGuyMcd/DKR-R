"""Keep checkpoint admission on its owner's single monotonic pulse timestamp."""
from pathlib import Path
import hashlib
AUTH=Path(__file__).resolve().parent
PRIOR_IDENTITY='893bbedb02a3c87771e75befab107a2ce54641073742be69e97b995a9d0a3bbf'

def once(text,before,after):
 if text.count(before)!=1:raise ValueError('Checkpoint clock anchor changed: '+before[:160])
 return text.replace(before,after,1)

def contract():
 return hashlib.sha256((AUTH/'transforms.py').read_bytes().replace(b'\r\n',b'\n')).hexdigest()

def runtime(text):
 return once(text,';const auto h=digest(', ';base+="'+contract()+'";const auto h=digest(')

def party_runtime(text):
 # pulse(now) drains packets before calling transfer(now). Starting a receiver
 # with a later Clock::now() made expired(now)'s backwards-clock guard fire in
 # that SAME pulse. It immediately sent Need after OfferAck, repeatedly retiring
 # valid offers before any data could arrive. Keep all packet transitions and
 # the subsequent expiry check on the same owner-supplied monotonic instant.
 text=once(text,'void message(MemberId source,std::span<const std::uint8_t> bytes) {',
  'void message(MemberId source,std::span<const std::uint8_t> bytes,Clock::time_point now) {')
 text=once(text,'try {message(packet.source,packet.bytes);}',
  'try {message(packet.source,packet.bytes,now);}')
 text=once(text,'message(local,envelope.b);','message(local,envelope.b,now);')
 text=once(text,'const auto now=Clock::now();auto& previous=requested_at[source];',
  'auto& previous=requested_at[source];')
 text=once(text,'receiver.begin(offer,match,world.contract().schema,offer.bytes,Clock::now(),e)',
  'receiver.begin(offer,match,world.contract().schema,offer.bytes,now,e)')
 text=once(text,'const auto result=receiver.receive(chunk,Clock::now(),e);',
  'const bool first=receiver.received_bytes()==0;\n            const auto result=receiver.receive(chunk,now,e);\n            if(first&&result==CheckpointReceiver::Result::Accepted)std::fprintf(stderr,"[party][checkpoint] first chunk received\\n");')
 start=text.index('    void message(');end=text.index('    void sample(',start)
 body=text[start:end]
 if body.count('phase_at=Clock::now()')!=2:raise ValueError('Unexpected message phase clocks')
 body=body.replace('phase_at=Clock::now()','phase_at=now')
 if 'Clock::now()' in body:raise ValueError('Packet handling still introduces a later timestamp')
 text=text[:start]+body+text[end:]
 # A duplicate offer also must not reset partial receiving/admission state.
 text=once(text,'if(offer.transfer==active_transfer&&(held||decoding.valid())){',
  'if(offer.transfer==active_transfer&&(receiver.active()||held||decoding.valid())){')
 text=once(text,'if(server())if(auto d=deliveries.find(source);d!=deliveries.end()&&d->second.sender.offer().transfer==transfer)d->second.accepted=true;',
 '''if(server())if(auto d=deliveries.find(source);d!=deliveries.end()&&d->second.sender.offer().transfer==transfer&&!d->second.accepted){
                d->second.accepted=true;d->second.progress=now;
                if(phase==Phase::Admission)std::fprintf(stderr,"[party][checkpoint] offer acknowledged\\n");
            }''')
 return once(text,'if(receiver.active()&&receiver.expired(now)) {receiver.cancel();receiving.reset();send(host,Msg::Need);}',
 '''if(receiver.active()&&receiver.expired(now)) {
            std::fprintf(stderr,"[party][checkpoint] expired; received=%zu; requesting a fresh transfer\\n",receiver.received_bytes());
            receiver.cancel();receiving.reset();send(host,Msg::Need);
        }''')
